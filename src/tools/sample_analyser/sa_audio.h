#ifndef SA_AUDIO_H
#define SA_AUDIO_H

#define SA_OP_C 6

typedef struct Ops Ops;

struct Ops {
	// 0 represents audio out, 1 to SA_OP_C are ops
	float phase[SA_OP_C + 1];
	float ratio[SA_OP_C + 1];
	float current_sample[SA_OP_C + 1];
	float mod_map[SA_OP_C + 1][SA_OP_C + 1];
	float base_freq;
	float sample_rate;
};

#endif
