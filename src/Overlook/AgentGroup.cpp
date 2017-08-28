#include "Overlook.h"

namespace Overlook {

AgentGroup::AgentGroup(System* sys) : sys(sys) {
	running = false;
	stopped = true;
	group_count = GROUP_COUNT;
	
	allowed_symbols.Add("AUDCAD");
	allowed_symbols.Add("AUDJPY");
	allowed_symbols.Add("AUDNZD");
	allowed_symbols.Add("AUDUSD");
	allowed_symbols.Add("CADJPY");
	allowed_symbols.Add("EURAUD");
	allowed_symbols.Add("EURCAD");
	allowed_symbols.Add("EURGBP");
	allowed_symbols.Add("EURJPY");
	allowed_symbols.Add("EURNZD");
	allowed_symbols.Add("EURUSD");
	allowed_symbols.Add("GBPAUD");
	allowed_symbols.Add("GBPCAD");
	allowed_symbols.Add("GBPJPY");
	allowed_symbols.Add("GBPNZD");
	allowed_symbols.Add("GBPUSD");
	allowed_symbols.Add("NZDCAD");
	allowed_symbols.Add("NZDJPY");
	allowed_symbols.Add("NZDUSD");
	allowed_symbols.Add("USDCAD");
	allowed_symbols.Add("USDJPY");

}

AgentGroup::~AgentGroup() {
	Stop();
}

void AgentGroup::Init() {
	ASSERT(sys);
	
	epsilon = 0.02;
	
	WhenInfo  << Proxy(sys->WhenInfo);
	WhenError << Proxy(sys->WhenError);
	
	ManagerLoader& loader = GetManagerLoader();
	Thread::Start(THISBACK(InitThread));
	loader.Run();
}

void AgentGroup::InitThread() {
	Progress(0, 6, "Refreshing work queue");
	MetaTrader& mt = GetMetaTrader();
	for(int j = 0; j < allowed_symbols.GetCount(); j++) {
		const String& allowed_sym = allowed_symbols[j];
		for(int i = 0; i < mt.GetSymbolCount(); i++) {
			const Symbol& sym = mt.GetSymbol(i);
			if (sym.IsForex() && (sym.name.Left(6)) == allowed_sym) {
				sym_ids.Add(i);
				break;
			}
		}
	}
	sym_count = sym_ids.GetCount();
	ASSERT(sym_count == SYM_COUNT);
	main_tf = sys->FindPeriod(1);
	ASSERT(main_tf != -1);
	
	int stoch_id = sys->Find<StochasticOscillator>();
	int osma_id = sys->Find<OsMA>();
	ASSERT(stoch_id != -1);
	ASSERT(osma_id != -1);
	
	indi_ids.Clear();
	indi_ids.Add().Set(osma_id).AddArg(5).AddArg(5*2).AddArg(5);
	indi_ids.Add().Set(osma_id).AddArg(15).AddArg(15*2).AddArg(15);
	indi_ids.Add().Set(osma_id).AddArg(240).AddArg(240*2).AddArg(240);
	indi_ids.Add().Set(osma_id).AddArg(1440).AddArg(1440*2).AddArg(1440);
	indi_ids.Add().Set(stoch_id).AddArg(5);
	indi_ids.Add().Set(stoch_id).AddArg(15);
	indi_ids.Add().Set(stoch_id).AddArg(240);
	indi_ids.Add().Set(stoch_id).AddArg(1440);
	
	RefreshWorkQueue();
	
	
	Progress(1, 6, "Refreshing data source and indicators");
	ProcessWorkQueue();
	ProcessDataBridgeQueue();
	
	
	Progress(2, 6, "Refreshing pointers of data sources");
	ResetValueBuffers();
	
	
	Progress(3, 6, "Reseting snapshots");
	RefreshSnapshots();
	
	
	Progress(5, 6, "Initializing agents and joiners");
	if (agents.GetCount() == 0)
		CreateAgents();
	for(int i = 0; i < agents.GetCount(); i++) {
		Agent& a = agents[i];
		
		const Price& askbid = mt.GetAskBid()[a.sym__];
		double ask = askbid.ask;
		double bid = askbid.bid;
		const Symbol& symbol = mt.GetSymbol(a.sym__);
		if (symbol.proxy_id != -1) {
			int j = sym_ids.Find(symbol.proxy_id);
			ASSERT(j != -1);
			a.proxy_id = j;
			a.proxy_base_mul = symbol.base_mul;
		} else {
			a.proxy_id = -1;
			a.proxy_base_mul = 0;
		}
		a.begin_equity = mt.AccountEquity();
		a.spread_points = ask - bid;
		ASSERT(a.spread_points > 0.0);
		
		a.Init();
	}
	ASSERT(agents.GetCount() == GROUP_COUNT * SYM_COUNT);
	agent_equities.SetCount(agents.GetCount() * snaps.GetCount(), 0.0);
	int agent_equities_mem = agent_equities.GetCount() * sizeof(double);
	
	max_memory_size = GetAmpDeviceMemory();
	agents_total_size = agents.GetCount() * sizeof(Agent);
	memory_for_snapshots = max_memory_size - agents_total_size - agent_equities_mem;
	snaps_per_phase = memory_for_snapshots / sizeof(Snapshot) * 8 / 10;
	snap_phase_count = snaps.GetCount() / snaps_per_phase;
	if (snaps.GetCount() % snaps_per_phase != 0) snap_phase_count++;
	snap_phase_id = 0,
	
	
	Progress(6, 6, "Complete");
}

void AgentGroup::Start() {
	Stop();
	running = true;
	stopped = false;
	Thread::Start(THISBACK(Main));
}

void AgentGroup::Stop() {
	running = false;
	while (stopped != true) Sleep(100);
}

void AgentGroup::SetAgentsTraining(bool b) {
	for(int i = 0; i < agents.GetCount(); i++)
		agents[i].is_training = b;
}

void AgentGroup::Main() {
	if (agents.GetCount() == 0 || sym_ids.IsEmpty() || indi_ids.IsEmpty()) return;
	
	RefreshSnapshots();
	
	#ifdef HAVE_SYSTEM_AMP
	Vector<Snapshot> snaps;
	#endif
	
	while (running) {
		if (phase == PHASE_SEEKSNAPS) {
			#ifdef HAVE_SYSTEM_AMP
			// Seek different snapshot dataset, because GPU memory is limited.
			if (snap_phase_id >= snap_phase_count)
				snap_phase_id = 0;
			
			int snap_begin = snap_phase_id * snaps_per_phase;
			int snap_end = Upp::min(snap_begin + snaps_per_phase, this->snaps.GetCount());
			int snap_count = snap_end - snap_begin;
			ASSERT(snap_count > 0);
			snaps.SetCount(snap_count);
			memcpy(snaps.Begin(), this->snaps.Begin() + snap_begin, snap_count * sizeof(Snapshot));
			snap_phase_id++;
			#endif
			
			phase = PHASE_TRAINING;
		}
		else if (phase == PHASE_TRAINING) {
			SetAgentsTraining(true);
			
			agent_equities.SetCount(agents.GetCount() * snaps.GetCount(), 0.0);
			RefreshEpsilon();
			
			int snap_count = snaps.GetCount();
			int agent_count = agents.GetCount();
			array_view<Snapshot, 1>  snap_view(snap_count, snaps.Begin());
			array_view<Agent, 1> agents_view(agent_count, agents.Begin());
			array_view<double, 1> equities_view(agent_equities.GetCount(), agent_equities.Begin());
			
			
			TimeStop ts;
			int64 total_elapsed = 0;
			for (int64 iter = 0; phase == PHASE_TRAINING && running; iter++) {
				
				// Change snapshot area, if needed, sometimes
				if (iter > 100) {
					total_elapsed += ts.Elapsed();
					ts.Reset();
					iter = 0;
					if (total_elapsed > 5*60*1000) {
						phase = PHASE_SEEKSNAPS;
						break;
					}
				}
				
				parallel_for_each(agents_view.extent, [=](index<1> idx) PARALLEL
			    {
			        int agent_id = idx[0];
			        Agent& agent = agents_view[idx];
			        int equities_begin = agent.id * snap_count;
			        
			        // Check cursor
			        if (agent.cursor <= 0 || agent.cursor >= snap_count)
						agent.ResetEpoch();
					
			        for(int i = 0; i < 1000; i++) {
			            Snapshot& cur_snap  = snap_view[agent.cursor - 0];
						Snapshot& prev_snap = snap_view[agent.cursor - 1];
						
						agent.timestep_actual--;
						
						agent.Main(cur_snap, prev_snap);
						
						// Get some diagnostic stats
						equities_view[equities_begin + agent.cursor] = agent.broker.AccountEquity();
						agent.cursor++;
						
						// Close all order at the end
						if (agent.cursor >= snap_count) {
							agent.ResetEpoch();
						}
			        }
			    });
			}
			
			agents_view.synchronize();
			equities_view.synchronize();
			
			
			SetAgentsTraining(false);
			
		}
		else if (phase == PHASE_JOINER) {
			
			int snap_count = snaps.GetCount();
			
			joiner_equities.SetCount(snap_count, 0.0);
			
			// Check cursor
	        if (joiner.cursor <= 0 || joiner.cursor >= snap_count)
				joiner.ResetEpoch();
			
	        while (phase == PHASE_JOINER && running) {
	            Snapshot& cur_snap  = snaps[joiner.cursor - 0];
				Snapshot& prev_snap = snaps[joiner.cursor - 1];
				
				joiner.timestep_actual--;
				
				joiner.Main(cur_snap, prev_snap);
				
				// Get some diagnostic stats
				joiner_equities[joiner.cursor] = joiner.broker.AccountEquity();
				joiner.cursor++;
				
				// Close all order at the end
				if (joiner.cursor >= snap_count) {
					joiner.ResetEpoch();
				}
	        }
		}
		else if (phase == PHASE_UPDATE) {
			
			RefreshSnapshots();
			
			// Updates latest snapshot signals
			
		}
		else if (phase == PHASE_REAL) {
			
			/*
			int wday = DayOfWeek(time);
			int shift = sys->GetShiftFromTimeTf(time, best_group->main_tf);
			if (prev_shift != shift) {
				if (wday == 0 || wday == 6) {
					// Do nothing
					prev_shift = shift;
				} else {
					sys->WhenInfo("Shift changed");
					
					// Forced askbid data download
					DataBridgeCommon& common = GetDataBridgeCommon();
					common.DownloadAskBid();
					common.RefreshAskBidData(true);
					
					// Refresh databridges
					best_group->ProcessDataBridgeQueue();
					
					// Use best group to set broker signals
					bool succ = best_group->PutLatest(mt);
					
					// Notify about successful signals
					if (succ) {
						prev_shift = shift;
						
						sys->WhenRealtimeUpdate();
					}
				}
			}
			
			// Check for market closing (weekend and holidays)
			else {
				Time after_hour = time + 60*60;
				int wday_after_hour = DayOfWeek(after_hour);
				if (wday == 5 && wday_after_hour == 6) {
					sys->WhenInfo("Closing all orders before market break");
					for(int i = 0; i < mt.GetSymbolCount(); i++) {
						mt.SetSignal(i, 0);
						mt.SetSignalFreeze(i, false);
					}
					mt.SignalOrders(true);
				}
				
				if (wday != 0 && wday != 6)
					best_group->Data();
			}
			*/
			
		}
		else if (phase == PHASE_WAIT) {
			
			
			// Changes to PHASE_UPDATE when time to update
			
		}
		else Sleep(100);
	}
	
	
	stopped = true;
}

void AgentGroup::LoadThis() {
	LoadFromFile(*this,	ConfigFile("agentgroup.bin"));
}

void AgentGroup::StoreThis() {
	Time t = GetSysTime();
	String file = ConfigFile("agentgroup.bin");
	bool rem_bak = false;
	if (FileExists(file)) {
		FileMove(file, file + ".bak");
		rem_bak = true;
	}
	StoreToFile(*this,	file);
	if (rem_bak)
		DeleteFile(file + ".bak");
}

void AgentGroup::Serialize(Stream& s) {
	s % agents % joiner % indi_ids % created % phase % group_count;
}

void AgentGroup::SetEpsilon(double d) {
	for(int i = 0; i < agents.GetCount(); i++)
		agents[i].dqn.SetEpsilon(d);
}

void AgentGroup::RefreshSnapshots() {
	ASSERT(buf_count != 0);
	ASSERT(sym_ids.GetCount() != 0);
	
	TimeStop ts;
	ProcessWorkQueue();
	
	int bars = sys->GetCountTf(main_tf) - data_begin;
	ASSERT(bars > 0);
	snaps.Reserve(bars + (60 - (bars % 60)));
	for(; counted_bars < bars; counted_bars++) {
		int shift = counted_bars + data_begin;
		Time t = sys->GetTimeTf(main_tf, shift);
		int wday = DayOfWeek(t);
		
		// Skip weekend
		if (wday == 0 || wday == 6) continue;
		
		Seek(snaps.Add(), shift);
	}
	ASSERT(snaps.GetCount() > 0);
	
	LOG("Refreshing snapshots took " << ts.ToString());
}

void AgentGroup::Progress(int actual, int total, String desc) {
	ManagerLoader& loader = GetManagerLoader();
	loader.PostProgress(actual, total, desc);
	loader.PostSubProgress(0, 1);
}

void AgentGroup::SubProgress(int actual, int total) {
	ManagerLoader& loader = GetManagerLoader();
	loader.PostSubProgress(actual, total);
}

void AgentGroup::ResetValueBuffers() {
	
	// Get total count of output buffers in the indicator list
	VectorMap<unsigned, int> bufout_ids;
	int buf_id = 0;
	for(int i = 0; i < indi_ids.GetCount(); i++) {
		FactoryDeclaration& decl = indi_ids[i];
		const FactoryRegister& reg = sys->GetRegs()[decl.factory];
		for(int j = decl.arg_count; j < reg.args.GetCount(); j++)
			decl.AddArg(reg.args[j].def);
		bufout_ids.Add(decl.GetHashValue(), buf_id);
		buf_id += reg.out[0].visible;
	}
	buf_count = buf_id;
	ASSERT(buf_count);
	
	
	// Get DataBridge core pointer for easy reading
	databridge_cores.SetCount(0);
	databridge_cores.SetCount(sys->GetSymbolCount(), NULL);
	int factory = sys->Find<DataBridge>();
	for(int i = 0; i < db_queue.GetCount(); i++) {
		CoreItem& ci = *db_queue[i];
		if (ci.factory != factory)
			continue;
		databridge_cores[ci.sym] = &*ci.core;
	}
	
	
	// Reserve zeroed memory for output buffer pointer vector
	value_buffers.Clear();
	value_buffers.SetCount(sym_ids.GetCount());
	for(int i = 0; i < sym_ids.GetCount(); i++)
		value_buffers[i].SetCount(buf_count, NULL);
	
	
	// Get output buffer pointer vector
	int total_bufs = 0;
	data_begin = 0;
	for(int i = 0; i < work_queue.GetCount(); i++) {
		CoreItem& ci = *work_queue[i];
		ASSERT(!ci.core.IsEmpty());
		const Core& core = *ci.core;
		const Output& output = core.GetOutput(0);
		
		int sym_id = sym_ids.Find(ci.sym);
		if (sym_id == -1)
			continue;
		if (ci.tf != main_tf)
			continue;
		
		DataBridge* db = dynamic_cast<DataBridge*>(&*ci.core);
		if (db) data_begin = Upp::max(data_begin, db->GetDataBegin());
		
		Vector<ConstBuffer*>& indi_buffers = value_buffers[sym_id];
		
		const FactoryRegister& reg = sys->GetRegs()[ci.factory];
		
		FactoryDeclaration decl;
		decl.Set(ci.factory);
		for(int j = 0; j < ci.args.GetCount(); j++) decl.AddArg(ci.args[j]);
		for(int j = decl.arg_count; j < reg.args.GetCount(); j++) decl.AddArg(reg.args[j].def);
		unsigned hash = decl.GetHashValue();
		
		
		// Check that args match to declaration
		#ifdef flagDEBUG
		ArgChanger ac;
		ac.SetLoading();
		ci.core->IO(ac);
		ASSERT(ac.args.GetCount() >= ci.args.GetCount());
		for(int i = 0; i < ci.args.GetCount(); i++) {
			int a = ac.args[i];
			int b = ci.args[i];
			if (a != b) {
				LOG(Format("%d != %d", a, b));
			}
			ASSERT(a == b);
		}
		#endif
		
		
		int buf_begin_id = bufout_ids.Find(hash);
		if (buf_begin_id == -1)
			continue;
		
		int buf_begin = bufout_ids[buf_begin_id];
		
		//LOG(i << ": " << ci.factory << ", " << sym_id << ", " << (int64)hash << ", " << buf_begin);
		
		for (int l = 0; l < reg.out[0].visible; l++) {
			int buf_pos = buf_begin + l;
			ConstBuffer*& bufptr = indi_buffers[buf_pos];
			ASSERT_(bufptr == NULL, "Duplicate work item");
			bufptr = &output.buffers[l];
			total_bufs++;
		}
	}
	int expected_total = sym_ids.GetCount() * buf_count;
	ASSERT_(total_bufs == expected_total, "Some items are missing in the work queue");
}

void AgentGroup::RefreshWorkQueue() {
	
	// Add proxy symbols to the queue if any
	Index<int> tf_ids, sym_ids;
	tf_ids.Add(main_tf);
	sym_ids <<= this->sym_ids;
	for(int i = 0; i < sym_ids.GetCount(); i++) {
		const Symbol& sym = GetMetaTrader().GetSymbol(sym_ids[i]);
		if (sym.proxy_id == -1) continue;
		sym_ids.FindAdd(sym.proxy_id);
	}
	sys->GetCoreQueue(work_queue, sym_ids, tf_ids, indi_ids);
	
	
	// Get DataBridge work queue
	Vector<FactoryDeclaration> db_indi_ids;
	db_indi_ids.Add().Set(sys->Find<DataBridge>());
	sys->GetCoreQueue(db_queue, sym_ids, tf_ids, db_indi_ids);
}

void AgentGroup::ProcessWorkQueue() {
	work_lock.Enter();
	
	for(int i = 0; i < work_queue.GetCount(); i++) {
		SubProgress(i, work_queue.GetCount());
		sys->Process(*work_queue[i]);
	}
	
	work_lock.Leave();
}

void AgentGroup::ProcessDataBridgeQueue() {
	work_lock.Enter();
	
	for(int i = 0; i < db_queue.GetCount(); i++) {
		SubProgress(i, db_queue.GetCount());
		sys->Process(*db_queue[i]);
	}
	
	work_lock.Leave();
}

bool AgentGroup::Seek(Snapshot& snap, int shift) {
	
	// Check that shift is not too much
	ASSERT_(shift >= 0 && shift < sys->GetCountTf(main_tf), "Data position is not in data range");
	ASSERT_(shift >= data_begin, "Data position is before common starting point");
	
	
	// Get some time values in binary format (starts from 0)
	Time t = sys->GetTimeTf(main_tf, shift);
	int month = t.month-1;
	int day = t.day-1;
	int hour = t.hour;
	int minute = t.minute;
	int wday = DayOfWeek(t);
	
	
	// Time sensor
	snap.year_timesensor = (month * 31.0 + day) / 372.0;
	snap.week_timesensor = ((wday * 24 + hour) * 60 + minute) / (7.0 * 24.0 * 60.0);
	snap.day_timesensor  = (hour * 60 + minute) / (24.0 * 60.0);
	
	
	// Refresh values (tf / sym / value)
	int k = 0;
	for(int i = 0; i < sym_ids.GetCount(); i++) {
		Vector<ConstBuffer*>& indi_buffers = value_buffers[i];
		for(int j = 0; j < buf_count; j++) {
			ConstBuffer& src = *indi_buffers[j];
			double d = src.GetUnsafe(shift);
			double pos, neg;
			if (d > 0) {
				pos = 1.0 - d;
				neg = 1.0;
			} else {
				pos = 1.0;
				neg = 1.0 + d;
			}
			snap.sensor[k++] = pos;
			snap.sensor[k++] = neg;
		}
		
		DataBridge& db = *dynamic_cast<DataBridge*>(databridge_cores[sym_ids[i]]);
		double open = db.GetBuffer(0).GetUnsafe(shift);
		snap.open[i] = open;
	}
	ASSERT(k == SENSOR_SIZE);
	
	
	// Reset signals
	for(int i = 0; i < SIGNAL_SIZE; i++)
		snap.signal[i] = 1.0;
	
	
	return true;
}

void AgentGroup::CreateAgents() {
	ASSERT(buf_count > 0);
	ASSERT(sym_count > 0);
	
	MetaTrader& mt = GetMetaTrader();
	agents.SetCount(sym_count * group_count);
	int j = 0;
	for(int group_id = 0; group_id < group_count; group_id++) {
		for(int i = 0; i < sym_ids.GetCount(); i++) {
			const Symbol& sym = mt.GetSymbol(sym_ids[i]);
			Agent& a = agents[j];
			a.id = j;
			a.sym_id = i;
			a.sym__ = sym_ids[i];
			a.group_id = group_id;
			a.Create();
			j++;
		}
	}
	ASSERT(j == agents.GetCount());
}

double AgentGroup::GetAverageDrawdown() {
	double dd = 0;
	for(int i = 0; i < agents.GetCount(); i++)
		dd += agents[i].last_drawdown;
	return dd / agents.GetCount();
}

double AgentGroup::GetAverageIterations() {
	double dd = 0;
	for(int i = 0; i < agents.GetCount(); i++)
		dd += agents[i].iter;
	return dd / agents.GetCount();
}

void AgentGroup::RefreshEpsilon() {
	double iters = GetAverageIterations();
	int level = iters / 10000;
	if (level <= 0)
		epsilon = 0.20;
	else if (level == 1)
		epsilon = 0.05;
	else if (level == 2)
		epsilon = 0.02;
	else if (level >= 3)
		epsilon = 0.01;
	for(int i = 0; i < agents.GetCount(); i++)
		agents[i].dqn.SetEpsilon(epsilon);
}

/*
void AgentGroup::Data() {
	if (last_datagather.Elapsed() < 5*60*1000)
		return;

	MetaTrader& mt = GetMetaTrader();
	Vector<Order> orders;
	Vector<int> signals;

	orders <<= mt.GetOpenOrders();
	signals <<= mt.GetSignals();

	mt.Data();

	int file_version = 1;
	double balance = mt.AccountBalance();
	double equity = mt.AccountEquity();
	Time time = mt.GetTime();

	FileAppend fout(ConfigFile(name + ".log"));
	int64 begin_pos = fout.GetSize();
	int size = 0;
	fout.Put(&size, sizeof(int));
	fout % file_version % balance % equity % time % signals % orders;
	int64 end_pos = fout.GetSize();
	size = end_pos - begin_pos - sizeof(int);
	fout.Seek(begin_pos);
	fout.Put(&size, sizeof(int));

	last_datagather.Reset();
}
*/

}
