#include "Overlook.h"

namespace Overlook {

AgentGroup::AgentGroup() {
	sys = NULL;
	
	agent_input_width = 0;
	group_input_width = 0;
	agent_input_height = 0;
	group_input_height = 0;
	data_size = 0;
	signal_size = 0;
	act_iter = 0;
	
	global_free_margin_level = 0.97;
	buf_count = 0;
	enable_training = true;
	sig_freeze = true;
}

AgentGroup::~AgentGroup() {
	Stop();
}

void AgentGroup::Progress(int actual, int total, String desc) {
	a0 = actual;
	t0 = total;
	prog_desc = desc;
	WhenProgress(actual, total, desc);
	SubProgress(0, 1);
}

void AgentGroup::SubProgress(int actual, int total) {
	a1 = actual;
	t1 = total;
	WhenSubProgress(actual, total);
}

void AgentGroup::SetEpsilon(double d) {
	dqn.SetEpsilon(d);
	for(int i = 0; i < agents.GetCount(); i++)
		agents[i].dqn.SetEpsilon(d);
}

void AgentGroup::CreateAgents() {
	ASSERT(buf_count > 0);
	
	MetaTrader& mt = GetMetaTrader();
	agents.SetCount(sym_ids.GetCount());
	for(int i = 0; i < sym_ids.GetCount(); i++) {
		const Symbol& sym = mt.GetSymbol(sym_ids[i]);
		Agent& a = agents[i];
		a.group = this;
		a.sym = sym_ids[i];
		a.group_id = i;
		a.proxy_sym = sym.proxy_id;
		a.Create(agent_input_width, agent_input_height);
	}
}

void AgentGroup::InitThreads() {
	ASSERT(buf_count != 0);
	ASSERT(tf_ids.GetCount() != 0);
	ASSERT(sym_ids.GetCount() != 0);
	ASSERT(agent_input_width != 0);
	ASSERT(group_input_width != 0);
	
	GenerateSnapshots();
}

void AgentGroup::Init() {
	ASSERT(sys);
	
	
	tf_periods.SetCount(tf_ids.GetCount(), 1);
	for(int i = 0; i < tf_ids.GetCount(); i++) {
		int period = sys->GetPeriod(tf_ids[i]) / sys->GetPeriod(tf_ids.Top());
		tf_periods[i] = period;
	}
	
	
	int indi;
	
	indi = sys->Find<Sensors>();
	ASSERT(indi != -1);
	indi_ids.Add(indi);
	
	
	RefreshWorkQueue();
	
	Progress(1, 6, "Processing data");
	ProcessWorkQueue();
	ProcessDataBridgeQueue();
	
	Progress(2, 6, "Finding value buffers");
	ResetValueBuffers();
	
	data_size = 1 + sym_ids.GetCount() * tf_ids.GetCount() * buf_count;
	signal_size = sym_ids.GetCount() * 2 * 3;
	total_size = data_size + signal_size;
	agent_input_width  = 1;
	agent_input_height = GetSignalPos(sym_ids.GetCount());
	group_input_width  = 1;
	group_input_height = GetSignalPos(sym_ids.GetCount()) + 1 + 1 + 2;
	
	Progress(3, 6, "Reseting snapshots");
	InitThreads();
	
	Progress(4, 6, "Initializing group trainee");
	group = this;
	if (agents.IsEmpty()) {
		Create(group_input_width, group_input_height);
	}
	TraineeBase::Init();
	
	Progress(5, 6, "Initializing agents");
	if (agents.IsEmpty()) {
		CreateAgents();
	}
	for(int i = 0; i < agents.GetCount(); i++) {
		Agent& a = agents[i];
		a.group = this;
		agents[i].Init();
	}
	
	Progress(0, 1, "Complete");
}

void AgentGroup::Start() {
	if (main_id != -1) return;
	act_iter = 0;
	main_id = sys->AddTaskBusy(THISBACK(Main));
	
	for(int i = 0; i < agents.GetCount(); i++) {
		Agent& a = agents[i];
		ASSERT(a.group);
		a.Start();
	}
}

void AgentGroup::Stop() {
	if (main_id == -1) return;
	sys->RemoveBusyTask(main_id);
	main_id = -1;
	while (at_main) Sleep(100);
	for(int i = 0; i < agents.GetCount(); i++) {
		Agent& a = agents[i];
		a.Stop();
	}
	StoreThis();
}

void AgentGroup::Main() {
	ASSERT(!at_main);
	at_main = true;
	epoch_total = snaps.GetCount();
	
	if (epoch_actual == 0) {
		prev_reward = 0;
	}
	
	// Do some action
	Action();
	
	
	// Store sequences periodically
	if ((act_iter % 1000) == 0 && last_store.Elapsed() > 60 * 60 * 1000) {
		StoreThis();
		last_store.Reset();
	}
	act_iter++;
	at_main = false;
}

void AgentGroup::Forward(Snapshot& snap, SimBroker& broker, Snapshot* next_snap) {
	
	// Input values
	// - data values
	//		- time value
	//		- data sensors
	//		- 'accum_buf'
	// - free-margin-level
	// - active instruments total / maximum
	// - account change sensor
	// - instrument value / (0.1 * equity) or something
	
	int snap_values = GetSignalPos(sym_ids.GetCount());
	int input_size = snap_values + 1 + 1 + 2;
	input_values.SetCount(input_size);
	ASSERT(input_size == group_input_height);
	
	
	// Copy common values from snapshot to input
	memcpy(input_values.Begin(), snap.values.Begin(), snap_values);
	
	// Additional sensor values
	int vpos = snap_values;
	input_values[vpos++] = global_free_margin_level;
	
	int active_count = 0;
    for(int i = 0; i < sym_ids.GetCount(); i++) {
		int sym = sym_ids[i];
		int signal;
		const Agent& a = agents[i];
		
		// Very minimum requirement: positive drawdown
		if (a.last_drawdown >= 0.50)
			signal = 0;
		else {
		    // Get signals from snapshots, where agents have wrote their latest signals.
		    int sig_pos = GetSignalPos(i);
		    double pos = 1.0 - snap.values[sig_pos];
		    double neg = 1.0 - snap.values[sig_pos + 1];
		    double dsignal = pos > 0.0 ? +pos : -neg;
		    
		    
		    if      (dsignal == 0.0) signal =  0;
			else if (dsignal >  0.0) {signal = +1; active_count++;}
			else if (dsignal <  0.0) {signal = -1; active_count++;}
			else Panic("Invalid action");
			
			
			// Set signal to broker
			if (!sig_freeze) {
				// Don't use signal freezing. Might cause unreasonable costs.
				broker.SetSignal(sym, signal);
				broker.SetSignalFreeze(sym, false);
			} else {
				// Set signal to broker, but freeze it if it's same than previously.
				int prev_signal = broker.GetSignal(sym);
				if (signal != prev_signal) {
					broker.SetSignal(sym, signal);
					broker.SetSignalFreeze(sym, false);
				} else {
					broker.SetSignalFreeze(sym, true);
				}
			}
		}
    }
    
    // Active sensor
	input_values[vpos++] = (double)active_count / sym_ids.GetCount();
	
	
	// Average reward sensor
	if (prev_reward > 0.0) {
		reward_average.Add(prev_reward);
		double max = reward_average.mean * 2.0;
		input_values[vpos++] = 1.0 - Upp::max(0.0, Upp::min(1.0, prev_reward / max));
		input_values[vpos++] = 1.0;
	}
	else if (prev_reward < 0.0) {
		loss_average.Add(-prev_reward);
		double max = loss_average.mean * 2.0;
		input_values[vpos++] = 1.0;
		input_values[vpos++] = 1.0 - Upp::max(0.0, Upp::min(1.0, -prev_reward / max));
	}
	else {
		input_values[vpos++] = 1.0;
		input_values[vpos++] = 1.0;
	}
	ASSERT(vpos == group_input_height);
	
	
	int action = dqn.Act(input_values);
	if      (action == ACT_NOACT) {
		
	}
	else if (action == ACT_INCSIG) {
		global_free_margin_level += 0.01;
		if (global_free_margin_level > 0.99)
			global_free_margin_level = 0.99;
	}
	else if (action == ACT_DECSIG) {
		global_free_margin_level -= 0.01;
		if (global_free_margin_level < 0.85)
			global_free_margin_level = 0.85;
	}
	else if (action == ACT_RESETSIG) {
		global_free_margin_level = 0.97;
	}
	else Panic("Invalid action");
	broker.SetFreeMargin(global_free_margin_level);
}

void AgentGroup::Backward(double reward) {
	prev_reward = reward;
	
	// pass to brain for learning
	dqn.Learn(reward);
	
	/*smooth_reward += reward;
	
	if (iter % 50 == 0) {
		smooth_reward /= 50;
		WhenRewardAverage(smooth_reward);
		smooth_reward = 0;
	}*/
	iter++;
}

void AgentGroup::StoreThis() {
	ASSERT(!name.IsEmpty());
	Time t = GetSysTime();
	String file = ConfigFile(name + ".agrp");
	bool rem_bak = false;
	if (FileExists(file)) {
		MoveFile(file, file + ".bak");
		rem_bak = true;
	}
	StoreToFile(*this,	file);
	if (rem_bak)
		DeleteFile(file + ".bak");
}

void AgentGroup::LoadThis() {
	LoadFromFile(*this,	ConfigFile(name + ".agrp"));
}

void AgentGroup::Serialize(Stream& s) {
	TraineeBase::Serialize(s);
	s % agents % tf_ids % sym_ids % created % name % param_str
	  % global_free_margin_level
	  % agent_input_width % agent_input_height
	  % group_input_width % group_input_height
	  % sig_freeze
	  % enable_training;
}

int AgentGroup::GetSignalBegin() const {
	return data_size;
}

int AgentGroup::GetSignalEnd() const {
	return total_size;
}

int AgentGroup::GetSignalPos(int group_id) const {
	ASSERT(group_id >= 0 && group_id <= sym_ids.GetCount());
	return data_size + group_id * 2 * 3;
}

void AgentGroup::RefreshSnapshots() {
	ProcessWorkQueue();
	
	int tf_snap = tf_ids.GetCount()-1;
	int main_tf = tf_ids[tf_snap];
	int pos = train_pos_all.Top()+1;
	int bars = sys->GetCountTf(main_tf);
	
	train_pos_all.Reserve(bars - pos);
	train_pos.SetCount(sym_ids.GetCount());
	for(int i = 0; i < train_pos.GetCount(); i++)
		train_pos[i].Reserve(bars - pos);
	
	for(int i = pos; i < bars; i++) {
		Time t = sys->GetTimeTf(main_tf, i);
		int wday = DayOfWeek(t);
		if (wday == 0 || wday == 6)
			continue;
		
		LOG("Agent::RefreshSnapshots: Creating snapshot at " << Format("%", t));
		One<Snapshot> snap;
		snap.Create();
		
		ResetSnapshot(*snap);
		Seek(*snap, i);
		
		snaps.Add(snap.Detach());
		int pospos = train_pos_all.GetCount();
		bool any_match = false;
		for(int j = 0; j < sym_ids.GetCount(); j++) {
			const Symbol& sym = GetMetaTrader().GetSymbol(sym_ids[j]);
			if (sym.IsOpen(t)) {
				train_pos[j].Add(pospos);
				any_match = true;
			}
		}
		if (any_match)
			train_pos_all.Add(i);
	}
}

void AgentGroup::GenerateSnapshots() {
	
	// Generate snapshots
	TimeStop ts;
	snaps.SetCount(train_pos_all.GetCount());
	for(int i = 0; i < train_pos_all.GetCount(); i++) {
		if (i % 87 == 0)
			SubProgress(i, train_pos_all.GetCount());
		Snapshot& snap = snaps[i];
		ResetSnapshot(snap);
		Seek(snap, train_pos_all[i]);
	}
	LOG("Generating snapshots took " << ts.ToString());
}

void AgentGroup::ResetSnapshot(Snapshot& snap) {
	ASSERT(!value_buffers.IsEmpty());
	//int buf_count = value_buffers[0].GetCount();
	
	//int tf_snap = tf_ids.GetCount()-1;
	//int main_tf = tf_ids[tf_snap];
	
	//snap.bars = sys->GetCountTf(main_tf);
	
	
	// Add periods and their multipliers to next longer timeframes
	/*int tf_count = tf_ids.GetCount();
	snap.pos.SetCount(tf_count, 0);
	int prev_period = 0;
	for(int j = 0; j < tf_count; j++) {
		int tf = tf_ids[j];
		int period = sys->GetPeriod(tf);
		snap.tfs.Add(tf);
		snap.periods.Add(period);
		if (prev_period == 0) snap.period_in_slower.Add(0);
		else                  snap.period_in_slower.Add(prev_period / period);
		prev_period = period;
	}*/
	
	
	// Reserve memory for values
	snap.values.SetCount(total_size, 0.0);
	snap.time_values.SetCount(5);
	
	
	// Get time range between shortest timeframe and now
	//snap.begin = sys->GetBegin(main_tf);
	//snap.begin_ts = sys->GetBeginTS(main_tf);
	
	
	// Seek to beginning
	Seek(snap, 0);
}

bool AgentGroup::Seek(Snapshot& snap, int shift) {
	int tf_snap = tf_ids.GetCount() - 1;
	int main_tf = tf_ids[tf_snap];
	int bars = sys->GetCountTf(main_tf);
	if (shift >= bars || shift < 0)
		return false;
	
	
	// Get some time values in binary format (starts from 0)
	Time t = sys->GetTimeTf(main_tf, shift);
	int month = t.month-1;
	int day = t.day-1;
	int hour = t.hour;
	int minute = t.minute;
	int dow = DayOfWeek(t);
	snap.time_values[0] = month;
	snap.time_values[1] = day;
	snap.time_values[2] = dow;
	snap.time_values[3] = hour;
	snap.time_values[4] = minute;
	snap.time = t;
	snap.added = GetSysTime();
	snap.shift = shift;
	
	
	// Find that time-position in longer timeframes
	/*snap.pos[tf_snap] = shift;
	for(int i = 0; i < tf_snap; i++) {
		int tf = snap.tfs[i];
		int slow_shift = sys->GetShiftTf(main_tf, tf, shift);
		ASSERT(slow_shift >= 0);
		snap.pos[i] = slow_shift;
	}*/
	
	
	// Time sensor
	int vpos = 0;
	double time_sensor = ((dow * 24 + hour) * 60 + minute) / (7.0 * 24.0 * 60.0);
	snap.values[vpos++] = time_sensor;
	
	
	// Refresh values (tf / sym / value)
	for(int i = 0; i < tf_ids.GetCount(); i++) {
		int pos = sys->GetShiftTf(main_tf, tf_ids[i], shift);
		for(int j = 0; j < sym_ids.GetCount(); j++) {
			Vector<ConstBuffer*>& indi_buffers = value_buffers[j][i];
			for(int k = 0; k < buf_count; k++) {
				ConstBuffer& src = *indi_buffers[k];
				double d = src.GetUnsafe(pos);
				snap.values[vpos + (j * tf_ids.GetCount() + i) * buf_count + k] = d;
			}
		}
	}
	
	return true;
}

void AgentGroup::RefreshWorkQueue() {
	Index<int> sym_ids;
	sym_ids <<= this->sym_ids;
	for(int i = 0; i < sym_ids.GetCount(); i++) {
		const Symbol& sym = GetMetaTrader().GetSymbol(i);
		if (sym.proxy_id == -1) continue;
		sym_ids.FindAdd(sym.proxy_id);
	}
	sys->GetCoreQueue(work_queue, sym_ids, tf_ids, indi_ids);
	
	// Get DataBridge work queue
	Index<int> db_tf_ids, db_indi_ids;
	db_tf_ids.Add(tf_ids.Top());
	db_indi_ids.Add(sys->Find<DataBridge>());
	sys->GetCoreQueue(db_queue, sym_ids, db_tf_ids, db_indi_ids);
}

void AgentGroup::ResetValueBuffers() {
	// Find value buffer ids
	VectorMap<int, int> bufout_ids;
	int buf_id = 0;
	for(int i = 0; i < indi_ids.GetCount(); i++) {
		bufout_ids.Add(indi_ids[i], buf_id);
		int indi = indi_ids[i];
		const FactoryRegister& reg = sys->GetRegs()[indi];
		buf_id += reg.out[0].visible;
	}
	buf_count = buf_id;
	ASSERT(buf_count);
	
	
	// Get DataBridge core pointer for easy reading
	databridge_cores.Clear();
	databridge_cores.SetCount(sys->GetSymbolCount(), NULL);
	int factory = sys->Find<DataBridge>();
	for(int i = 0; i < db_queue.GetCount(); i++) {
		CoreItem& ci = *db_queue[i];
		if (ci.factory != factory) continue;
		databridge_cores[ci.sym] = &*ci.core;
	}
	
	
	// Reserve memory for value buffer vector
	value_buffers.Clear();
	value_buffers.SetCount(sym_ids.GetCount());
	for(int i = 0; i < sym_ids.GetCount(); i++) {
		value_buffers[i].SetCount(tf_ids.GetCount());
		for(int j = 0; j < value_buffers[i].GetCount(); j++)
			value_buffers[i][j].SetCount(buf_count, NULL);
	}
	
	
	// Get value buffers
	int total_bufs = 0;
	data_begins.SetCount(tf_ids.GetCount(), 0);
	for(int i = 0; i < work_queue.GetCount(); i++) {
		CoreItem& ci = *work_queue[i];
		ASSERT(!ci.core.IsEmpty());
		const Core& core = *ci.core;
		const Output& output = core.outputs[0];
		
		int sym_id = sym_ids.Find(ci.sym);
		if (sym_id == -1) continue;
		int tf_id = tf_ids.Find(ci.tf);
		if (tf_id == -1) continue;
		
		DataBridge* db = dynamic_cast<DataBridge*>(&*ci.core);
		if (db) data_begins[tf_id] = Upp::max(data_begins[tf_id], db->GetDataBegin());
		
		Vector<ConstBuffer*>& indi_buffers = value_buffers[sym_id][tf_id];
		
		const FactoryRegister& reg = sys->GetRegs()[ci.factory];
		int buf_begin = bufout_ids.Find(ci.factory);
		if (buf_begin == -1) continue;
		buf_begin = bufout_ids[buf_begin];
		
		for (int l = 0; l < reg.out[0].visible; l++) {
			int buf_pos = buf_begin + l;
			ConstBuffer*& bufptr = indi_buffers[buf_pos];
			ASSERT_(bufptr == NULL, "Duplicate work item");
			bufptr = &output.buffers[l];
			total_bufs++;
		}
	}
	int expected_total = sym_ids.GetCount() * tf_ids.GetCount() * buf_count;
	ASSERT_(total_bufs == expected_total, "Some items are missing in the work queue");
	
	
	int main_tf = tf_ids.Top();
	int pos = data_begins.Top();
	int bars = sys->GetCountTf(main_tf);
	
	train_pos_all.Reserve(bars - pos);
	train_pos.SetCount(sym_ids.GetCount());
	for(int i = 0; i < train_pos.GetCount(); i++)
		train_pos[i].Reserve(bars - pos);
	
	for(int i = pos; i < bars; i++) {
		Time t = sys->GetTimeTf(main_tf, i);
		int wday = DayOfWeek(t);
		if (wday == 0 || wday == 6)
			continue;
		int pospos = train_pos_all.GetCount();
		bool any_match = false;
		for(int j = 0; j < sym_ids.GetCount(); j++) {
			const Symbol& sym = GetMetaTrader().GetSymbol(sym_ids[j]);
			if (sym.IsOpen(t)) {
				train_pos[j].Add(pospos);
				any_match = true;
			}
		}
		if (any_match)
			train_pos_all.Add(i);
	}
}

void AgentGroup::ProcessWorkQueue() {
	for(int i = 0; i < work_queue.GetCount(); i++) {
		//LOG(i << "/" << work_queue.GetCount());
		SubProgress(i, work_queue.GetCount());
		sys->Process(*work_queue[i]);
	}
}

void AgentGroup::ProcessDataBridgeQueue() {
	for(int i = 0; i < db_queue.GetCount(); i++) {
		//LOG(i << "/" << db_queue.GetCount());
		SubProgress(i, db_queue.GetCount());
		sys->Process(*db_queue[i]);
	}
}

void AgentGroup::SetAskBid(SimBroker& sb, int pos) {
	for(int i = 0; i < databridge_cores.GetCount(); i++) {
		if (!databridge_cores[i]) continue;
		Core& core = *databridge_cores[i];
		ConstBuffer& open = core.GetBuffer(0);
		sb.SetPrice(core.GetSymbol(), open.Get(pos));
	}
	sb.SetTime(sys->GetTimeTf(tf_ids.Top(), pos));
}

}
