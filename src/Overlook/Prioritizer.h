#ifndef _Overlook_Prioritizer_h_
#define _Overlook_Prioritizer_h_

#include <CtrlUtils/CtrlUtils.h>
#include <CoreUtils/CoreUtils.h>

namespace Overlook {
using namespace Upp;

class Overlook;

typedef Tuple2<int, int> IntPair;

template <class T>
inline bool ReadBit(const Vector<T>& value, int bit) {
	int size = sizeof(T) * 8;
	int byt = bit / size;
	bit = bit % size;
	ASSERT(byt >= 0 && byt < value.GetCount());
	typedef const T ConstT;
	ConstT* b = value.Begin() + byt;
	T mask = 1 << bit;
	return *b & mask;
}

struct Combination : Moveable<Combination> {
	Vector<byte> value;		// Boolean values of settings.
	//Vector<byte> dep_mask;	// Mask of meaningful unique values of slot (currently selected)
	//Vector<byte> exp_mask;	// Mask of meaningful unique values of slots depending on this slot (currently selected)
	
	
	Combination() {}
	Combination(const Combination& comb) {*this = comb;}
	void operator=(const Combination& comb) {
		value		<<= comb.value;
		//dep_mask	<<= comb.dep_mask;
		//exp_mask	<<= comb.exp_mask;
	}
	
	void Zero();
	
	bool GetValue(int bit) const;
	
	void SetSize(int bytes);
	void SetValue(int bit, bool value);
	
};

// Combination
struct CombinationResult : Moveable<CombinationResult> {
	Vector<byte> value; // combination
	
};

struct CombinationPart : Moveable<CombinationPart> {
	Vector<ValueType> inputs;
	Vector<Vector<IntPair> > input_src;
	int begin, end, size;
	bool single_sources;
};

class CorelineItem : Moveable<CorelineItem> {
	
public:
	typedef CorelineItem CLASSNAME;
	CorelineItem() {priority = 0;}
	
	
	Vector<byte> value;
	int priority;
	
};

class CoreItem : Moveable<CoreItem> {
	
public:
	typedef CoreItem CLASSNAME;
	CoreItem() {priority = 0;}
	
	
	VectorMap<int, int> symlist, tflist;
	Vector<byte> pipeline_src;
	Vector<byte> value;
	int priority;
	int factory;
	
};

class JobItem : Moveable<JobItem> {
	
public:
	typedef JobItem CLASSNAME;
	JobItem() {
		core = NULL;
	}
	
	Vector<byte> value; // combination
	Core* core;
	int64 priority;
	int factory;
	int sym, tf;
	
};

class Prioritizer {
	
	// Jobber
	
protected:
	
	Vector<CorelineItem> pl_queue;
	Vector<CoreItem> slot_queue;
	Vector<JobItem> job_queue;
	BaseSystem bs;
	
	void RefreshCoreQueue();
	void RefreshJobQueue();
	void VisitSymTf(Combination& comb);
	void VisitSymTf(int fac_id, int sym_id, int tf_id, Combination& comb);
	void VisitCombination(int factory, Combination& com);
	
public:
	void CreateNormal();
	void CreateSingle(int main_fac_id, int sym_id, int tf_id);
	
	int GetCorelineCount() const {return pl_queue.GetCount();}
	CorelineItem& GetCoreline(int i) {return pl_queue[i];}
	int GetCoreCount() const {return slot_queue.GetCount();}
	CoreItem& GetCore(int i) {return slot_queue[i];}
	int GetJobCount() const {return job_queue.GetCount();}
	JobItem& GetJob(int i) {return job_queue[i];}
	BaseSystem& GetBaseSystem() {return bs;}
	
	
	
protected:
	
	AtomicInt cursor;
	int thread_count;
	bool running, stopped;
	
	Vector<CombinationResult> results;
	Vector<CombinationPart> combparts;
	Vector<int> inputs_to_enabled, enabled_to_factory;
	int combination_bits, combination_bytes, combination_errors;
	
	
	// Utils
	bool IsBegin() const;
	int GetBitTf(int tf_id) const;
	int GetBitSym(int sym_id) const;
	int GetBitCore(int fac_id, int input_id, int src_id) const;
	int GetBitEnabled(int fac_id) const;
	int InputToEnabled(int bit) const;
	int EnabledToFactory(int bit) const;
	
	
	typedef Vector<VectorMap<uint32, VectorMap<int, VectorMap<int, Core*> > > > CoreData;
	static CoreData& GetCoreData() {static CoreData data; return data;}
	static SpinLock& Lock() {static SpinLock lock; return lock;}
	
	void RefreshQueue();
	void ProcessThread(int thread_id);
	void Process(CoreItem& qi);
	void Run();
	
public:
	typedef Prioritizer CLASSNAME;
	Prioritizer();
	~Prioritizer();
	
	virtual void Init();
	virtual void Deinit();
	
	
	void CreateCombination();
	void Start() {Stop(); running = true; stopped = false; Thread::Start(THISBACK(Run));}
	void Stop() {running = false; while (!stopped) Sleep(100);}
	void Process();
	
	//Core& GetCore(int fac_id, uint32 input_comb, int sym, int tf, VectorMap<String, Value>* args=NULL);
	//Core& GetCore(int fac_id, int sym_id, int tf_id);
	
	Mutex lock;
};

}

#endif
