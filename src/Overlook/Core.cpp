#include "Overlook.h"

namespace Overlook {

/*Core::Core() {
	
}

Core::~Core() {
	
}*/
/*
void Core::AddValue(uint16 bytes, String name, String description) {
	CoreValue& value = values.Add();
	value.bytes = bytes;
	value.name = name;
	value.description = description;
	data.Add();
}

void Core::SetReady(int sym_id, int tf_id, int pos, const CoreProcessAttributes& attr, bool ready) {
	byte* ready_slot = this->ready[sym_id][tf_id].Begin();
	ASSERT(ready_slot);
	int slot_pos = attr.slot_pos;
	int ready_bit = slot_pos % 8;
	ready_slot += slot_pos / 8;
	byte ready_mask = 1 << ready_bit;
	if (ready)
		*ready_slot |= ready_mask;
	else
		*ready_slot &= !ready_mask;
}

void Core::SetReady(int pos, const CoreProcessAttributes& attr, bool ready) {
	SetReady(attr.sym_id, attr.tf_id, pos, attr, ready);
}

bool Core::IsReady(int pos, const CoreProcessAttributes& attr) {
	byte* ready_slot = ready[attr.sym_id][attr.tf_id].Begin();
	if (!ready_slot)
		return false;
	int slot_pos = attr.slot_pos;
	int ready_bit = slot_pos % 8;
	ready_slot += slot_pos / 8;
	byte ready_mask = 1 << ready_bit;
	return ready_mask & *ready_slot;
}
*/


Core::Core()
{
	window_type = WINDOW_CHART;
	bars = 0;
	next_count = 0;
	counted = 0;
	has_maximum = 0;
	has_minimum = 0;
	maximum = 0;
	minimum = 0;
	skip_setcount = false;
	point = 0.01;
	sym_id = -1;
	tf_id = -1;
}

Core::~Core() {
	
}

void Core::ClearContent() {
	bars = 0;
	counted = 0;
	for(int i = 0; i < indices.GetCount(); i++) {
		FloatVector& id = *indices[i];
		id.Clear();
	}
}


void Core::Refresh()
{
	/*
	lock.EnterWrite();
	
	if (src.Is()) {
		Core* cont = src.Get<Core>();
		if (cont)
			cont->Refresh();
	}
		
	if (!skip_setcount) {
		BarData* src_data = src.Get<BarData>();
		if (src_data) {
			int count = src_data->GetBars();
			for(int i = 0; i < indices.GetCount(); i++) {
				indices[i]->SetCount(count);
			}
			bars = count;
			next_count = count;
		}
	}
	
	RefreshDependencies();
	Start();

	counted = next_count;
	lock.LeaveWrite();
	*/
}

void Core::SetIndexBuffer ( int i, FloatVector& vec)
{
	indices[i] = &vec;
	if (i < buffer_settings.GetCount())
		buffer_settings[i].buffer = &vec;
}

void Core::SetPoint(double d) {
	point = d;
	for(int i = 0; i < indices.GetCount(); i++) {
		indices[i]->SetPoint(d);
	}
}








double Core::Open ( int shift ) {
	//return data->GetIndexValue(0, shift);
}

double Core::High( int shift ) {
	//return data->GetIndexValue(2, shift);
}

double Core::Low( int shift ) {
	//return data->GetIndexValue(1, shift);
}

double Core::Volume ( int shift ) {
	//return data->GetIndexValue(3, shift);
}

int Core::HighestHigh(int period, int shift) {
	ASSERT(period > 0);
	double highest = -DBL_MAX;
	int highest_pos = -1;
	for (int i = 0; i < period && shift >= 0; i++, shift--) {
		double high = High(shift);
		if (high > highest) {
			highest = high;
			highest_pos = shift;
		}
	}
	return highest_pos;
}

int Core::LowestLow(int period, int shift) {
	ASSERT(period > 0);
	double lowest = DBL_MAX;
	int lowest_pos = -1;
	for (int i = 0; i < period && shift >= 0; i++, shift--) {
		double low = Low(shift);
		if (low < lowest) {
			lowest = low;
			lowest_pos = shift;
		}
	}
	return lowest_pos;
}

int Core::HighestOpen(int period, int shift) {
	ASSERT(period > 0);
	double highest = -DBL_MAX;
	int highest_pos = -1;
	for (int i = 0; i < period && shift >= 0; i++, shift--) {
		double open = Open(shift);
		if (open > highest) {
			highest = open;
			highest_pos = shift;
		}
	}
	return highest_pos;
}

int Core::LowestOpen(int period, int shift) {
	ASSERT(period > 0);
	double lowest = DBL_MAX;
	int lowest_pos = -1;
	for (int i = 0; i < period && shift >= 0; i++, shift--) {
		double open = Open(shift);
		if (open < lowest) {
			lowest = open;
			lowest_pos = shift;
		}
	}
	return lowest_pos;
}

int Core::GetMinutePeriod() {
	
}

double Core::GetAppliedValue ( int applied_value, int i )
{
	double dValue;
	
	switch ( applied_value )
	{

		case 0:
			dValue = Open(i);
			break;

		case 1:
			dValue = High(i);
			break;

		case 2:
			dValue = Low(i);
			break;

		case 3:
			dValue =
				( High(i) + Low(i) )
				/ 2.0;
			break;

		case 4:
			dValue =
				( High(i) + Low(i) + Open(i) )
				/ 3.0;
			break;

		case 5:
			dValue =
				( High(i) + Low(i) + 2 * Open(i) )
				/ 4.0;
			break;

		default:
			dValue = 0.0;
	}
	return ( dValue );
}

void Core::RefreshSource() {
	/*data = src.Get<BarData>();
	if (data)
		SetPoint(data->GetPoint());*/
}

}
