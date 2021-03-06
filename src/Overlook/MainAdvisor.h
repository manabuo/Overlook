#ifndef _Overlook_MainAdvisor_h_
#define _Overlook_MainAdvisor_h_

namespace Overlook {
using namespace Upp;

#define DEBUG_BUFFERS 1

class MainAdvisor : public Core {
	
protected:
	friend class AccountAdvisor;
	friend class WeekSlotAdvisor;
	
	static const int SYM_BITS			= 4;
	static const int tf_count			= 3;
	static const int week_bits			= 5 * 24 * 4;
	static const int TIME_BITS			= 5 + 24 + 4;
	static const int INPUT_SIZE			= TIME_BITS + (SYM_COUNT+1) * ASSIST_COUNT * tf_count;
	static const int OUTPUT_SIZE		= (SYM_COUNT+1) * SYM_BITS * tf_count;
;
	static const int CORE_COUNT			= 25;
	
	static const int check_period1		= 2;
	static const int check_period2		= 2*4;
	
	struct TrainingDQNCtrl : public JobCtrl {
		Vector<Point> polyline;
		virtual void Paint(Draw& w);
	};
	
	
	typedef DQNTrainer<OUTPUT_SIZE, INPUT_SIZE, 100> DQN;
	
	
	// Persistent
	Vector<DQN::DQVectorType>	data;
	DQN							dqn_trainer;
	Vector<double>				dqntraining_pts;
	int							dqn_round			= 0;
	int							dqn_pt_cursor		= 0;
	int							main_begin = 0;
	
	
	
	// Temp
	ConstBuffer*				open_buf[(SYM_COUNT+1) * tf_count];
	DQN::MatType				tmp_before_state, tmp_after_state;
	Vector<CoreIO*>				cores;
	VectorBool					tmp_assist;
	SimBroker					sb;
	int							tf_ids[tf_count];
	int							tf_step[tf_count];
	int							tf_div[tf_count];
	int							prev_counted		= 0;
	int							realtime_count		= 0;
	double						check_sum1[SYM_COUNT];
	double						check_sum2[SYM_COUNT];
	double						spread_point[SYM_COUNT+1];
	bool						priority_bits[SYM_COUNT * week_bits];
	bool						once				= true;
	#ifdef flagDEBUG
	int							dqn_max_rounds		= 500;
	#else
	int							dqn_max_rounds		= 5000000;
	#endif
	
protected:
	virtual void Start();
	
	bool TrainingDQNBegin();
	bool TrainingDQNIterator();
	bool TrainingDQNEnd();
	bool TrainingDQNInspect();
	void RunMain();
	void RefreshOutputBuffers();
	void RefreshMain();
	void RefreshAll();
	void RefreshAction(int data_pos);
	void RefreshReward(int data_pos);
	void LoadState(DQN::MatType& state, int cursor);
	void MainReal();
	void RunSimBroker();
	
public:
	typedef MainAdvisor CLASSNAME;
	MainAdvisor();
	
	virtual void Init();
	
	
	double GetSpreadPoint(int i) const {return spread_point[i];}
	
	virtual void IO(ValueRegister& reg) {
		reg % In<DataBridge>(&FilterFunction2)
			
			% In<MovingAverage>(&FilterFunction1)
			% In<MovingAverageConvergenceDivergence>(&FilterFunction1)
			% In<BollingerBands>(&FilterFunction1)
			% In<ParabolicSAR>(&FilterFunction1)
			% In<StandardDeviation>(&FilterFunction1)
			% In<AverageTrueRange>(&FilterFunction1)
			% In<BearsPower>(&FilterFunction1)
			% In<BullsPower>(&FilterFunction1)
			% In<CommodityChannelIndex>(&FilterFunction1)
			% In<DeMarker>(&FilterFunction1)
			% In<ForceIndex>(&FilterFunction1)
			% In<Momentum>(&FilterFunction1)
			% In<RelativeStrengthIndex>(&FilterFunction1)
			% In<RelativeVigorIndex>(&FilterFunction1)
			% In<StochasticOscillator>(&FilterFunction1)
			% In<AcceleratorOscillator>(&FilterFunction1)
			% In<AwesomeOscillator>(&FilterFunction1)
			% In<PeriodicalChange>(&FilterFunction1)
			% In<VolatilityAverage>(&FilterFunction1)
			% In<VolatilitySlots>(&FilterFunction1)
			% In<VolumeSlots>(&FilterFunction1)
			% In<ChannelOscillator>(&FilterFunction1)
			% In<ScissorChannelOscillator>(&FilterFunction1)
			% In<CommonForce>(&FilterFunction1)
			
			% Out(1, 1)
			% Mem(data)
			% Mem(dqn_trainer)
			% Mem(dqntraining_pts)
			% Mem(dqn_round)
			% Mem(dqn_pt_cursor)
			% Mem(main_begin);
	}
	
	static bool FilterFunction1(void* basesystem, int in_sym, int in_tf, int out_sym, int out_tf) {
		if (in_sym == -1) {
			int mins = ::Overlook::GetSystem().GetPeriod(out_tf);
			return mins == 15 || (tf_count > 1 && mins == 60) || (tf_count > 2 && mins == 240);
		}

		static int strong_sym;
		if (strong_sym == 0) strong_sym = ::Overlook::GetSystem().GetCommonSymbol();
		
		return out_sym == strong_sym || ::Overlook::GetSystem().GetSymbolPriority(out_sym) < SYM_COUNT;
	}
	
	static bool FilterFunction2(void* basesystem, int in_sym, int in_tf, int out_sym, int out_tf) {
		if (in_sym == -1) {
			int mins = ::Overlook::GetSystem().GetPeriod(out_tf);
			return mins == 15 || (tf_count > 1 && mins == 60) || (tf_count > 2 && mins == 240);
		}
		
		if (in_sym == out_sym) return true;
		
		static int strong_sym;
		if (strong_sym == 0) strong_sym = ::Overlook::GetSystem().GetCommonSymbol();
		
		return out_sym == strong_sym || ::Overlook::GetSystem().GetSymbolPriority(out_sym) < SYM_COUNT;
	}
	

};


}

#endif
