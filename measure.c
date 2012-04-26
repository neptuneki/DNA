#include "measure.h"
#include "main.h" //for TIME_FACTOR, TODO put somewhere else
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>

/* STUFF FOR REDIRECTING STDOUT. POSIX-ONLY. */

typedef struct {
	int newfd; /* fd we want to switch to */
} StreamState;

static StreamState makeStreamState(const char *filename)
{
	StreamState s;
	s.newfd = open(filename, O_WRONLY | O_CREAT, 0644);
	if (s.newfd < 0) {
		perror("Error opening file");
		assert(false);
	}
	return s;
}

/* Switch the current stdout to the stream given in s.
 * At the end of this function: s->fd is an fd to stdout as it was when 
 * calling this fuction; s->pos is the position in that stream an that 
 * time.
 * If s is NULL, or s.newfd < 0: do noting. */
static void switchStdout(StreamState *s)
{
	if (s == NULL  ||  s->newfd < 0)
		return;
	
	fflush(stdout);
	int oldStdout = dup(1);
	dup2(s->newfd, 1);
	close(s->newfd);
	s->newfd = oldStdout;
}

/* Flushes the new stream in s and closes it. */
static void stopRedirection(StreamState *s)
{
	if (s == NULL  ||  s->newfd < 0)
		return;
	close(s->newfd);
	s->newfd = 0;
}



/* SOME WRAPPERS FOR SAMPLERS: */

/* Returns the state pointer that gets returned from sampler.start(), or 
 * NULL if sampler.start == NULL */
static void *samplerStart(Sampler *sampler)
{
	if (sampler->start == NULL)
		return NULL;
	return sampler->start(sampler->samplerConf);
}

/* Sample and return sampler.sample(), or do nothing if sampler.sample == 
 * NULL and return true. Note that this would be a pretty useless sampler 
 * in the latter case... */
static bool samplerSample(Sampler *sampler, SamplerData *sd, void *state)
{
	if (sampler->sample == NULL)
		return true;
	return sampler->sample(sd, state);
}

/* Stop the sampler, or do nothing if sampler.stop == NULL */
static void samplerStop(Sampler *sampler, SamplerData *sd, void *state)
{
	if (sampler->stop == NULL)
		return;
	sampler->stop(sd, state);
}



/* MEASUREMENT TASK STUFF */

typedef struct measTaskState
{
	Sampler sampler;
	void *samplerState;
	SamplerData samplerData;
	enum {RELAXING, SAMPLING} measStatus;
	MeasurementConf measConf;
	double intervalTime; /* Time since last sample (or start). */
	StreamState streamState; /* For stdout redirection */
} MeasTaskState;

static void *measStart(void *initialData)
{
	Measurement *meas = (Measurement*) initialData;
	assert(meas != NULL);
	Sampler *sampler = &meas->sampler;
	MeasTaskState *state = malloc(sizeof(*state));

	if (meas->measConf.measureFile != NULL) {
		state->streamState = makeStreamState(
				meas->measConf.measureFile);
	} else {
		state->streamState.newfd = -1;
	}

	state->intervalTime = 0;
	state->sampler = meas->sampler; /* struct copy */
	state->measConf = meas->measConf; /* struct copy */
	state->measStatus = (meas->measConf.measureWait > 0 ?
				RELAXING : SAMPLING);
	state->samplerData.sample = 0;
	state->samplerData.strBufSize = meas->measConf.renderStrBufSize;
	state->samplerData.string = (meas->measConf.renderStrBufSize > 0 ?
			malloc(meas->measConf.renderStrBufSize) : NULL);

	switchStdout(&state->streamState); /* Switch stdout to file */
	state->samplerState = samplerStart(sampler);
	switchStdout(&state->streamState); /* Switch stdout back */

	free(initialData);
	return state;
}

static bool measTick(void *state)
{
	MeasTaskState *measState = (MeasTaskState*) state;
	assert(measState != NULL);
	MeasurementConf *measConf = &measState->measConf;
	Sampler *sampler = &measState->sampler;
	double measWait = measConf->measureWait;
	double measInterval = measConf->measureInterval;
	double time = getTime();
	bool ret = true;

	if (measInterval < 0)
		return ret;


	switch (measState->measStatus) {
	case RELAXING:
		if (fmod(time, measWait / 100) < config.timeStep) {
			printf("\rRelax time %13f of %f",
					(time + measWait/100) / TIME_FACTOR, 
					measWait / TIME_FACTOR);
			fflush(stdout);
		}
		if (time >= measWait) {
			measState->intervalTime = time - measWait;
			printf("\nStarting measurement.\n");
			measState->measStatus = SAMPLING;
		}
		break;
	case SAMPLING:
		measState->intervalTime += config.timeStep; //TODO nicer?

		if (measState->intervalTime < measInterval)
			break;

		measState->intervalTime -= measInterval;
		switchStdout(&measState->streamState); /* Switch stdout to file */
		ret = samplerSample(sampler, &measState->samplerData, 
				measState->samplerState);
		switchStdout(&measState->streamState); /* Switch stdout back */
		measState->samplerData.sample++;
		
		if (measConf->measureSamples < 0)
			break; /* Go on indefinitely, don't print anything */

		printf("\rMeasured sample %ld/%ld", 
				measState->samplerData.sample,
				measConf->measureSamples);
		fflush(stdout);
		if (measState->samplerData.sample >= measConf->measureSamples) {
			printf("\nFinished sampling!\n");
			ret = false; /* Stop running */
		}
		break;
	default:
		fprintf(stderr, "Unknown measurement status!\n");
		assert(false);
	}


	return ret;
}

static void measStop(void *state)
{
	MeasTaskState *measState = (MeasTaskState*) state;
	Sampler *sampler = &measState->sampler;

	switchStdout(&measState->streamState); /* Switch stdout to file */
	samplerStop(sampler, &measState->samplerData, measState->samplerState);
	switchStdout(&measState->streamState); /* Switch stdout back */
	stopRedirection(&measState->streamState);

	free(measState->samplerData.string);
	free(measState);
}



Task measurementTask(Measurement *measurement)
{
	/* We must make a copy because given pointer is not guaranteed to 
	 * remain valid. */
	Measurement *measCpy = malloc(sizeof(*measCpy));
	memcpy(measCpy, measurement, sizeof(*measCpy));

	/* Make task */
	Task task;
	task.initialData = measCpy;
	task.start = &measStart;
	task.tick  = &measTick;
	task.stop  = &measStop;

	return task;
}


