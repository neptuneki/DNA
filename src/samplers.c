#include <string.h>
#include <stdio.h>
#include "samplers.h"
#include "physics.h"
#include "integrator.h"
#include "spgrid.h"
#include "octave.h"

/* Simple sampler start that just passes the configuration data as the 
 * state pointer. */
static void *passConf(SamplerData *sd, void *conf)
{
	UNUSED(sd);
	return conf;
}
/* Simple sampler stop that just frees the state. */
static void freeState(SamplerData *sd, void *state)
{
	UNUSED(sd);
	free(state);
}


/* TEMPERATURE */

static SamplerSignal tempSample(SamplerData *sd, void *data)
{
	UNUSED(sd);
	UNUSED(data);
	printf("%le %lf %lf\n", getTime(), getHeatBathTemperature(),
					getKineticTemperature());
	return SAMPLER_OK;
}
Sampler temperatureSampler(void)
{
	Sampler sampler;
	sampler.samplerConf = NULL;
	sampler.start  = NULL;
	sampler.sample = &tempSample;
	sampler.stop   = NULL;
	sampler.header ="# <time> <heat bath temperature> "
			"<kinetic temperature>\n";
	return sampler;
}


/* AVERAGE TEMPERATURE */

static void *avgTempStart(SamplerData *sd, void *conf)
{
	UNUSED(sd);
	UNUSED(conf);
	double *accum = malloc(sizeof *accum); //yes, this is a silly malloc :P
	*accum = 0;
	return accum;
}
static SamplerSignal avgTempSample(SamplerData *sd, void *data)
{
	UNUSED(sd);
	double *accum = (double*) data;
	*accum += getKineticTemperature();
	return SAMPLER_OK;
}
static void avgTempStop(SamplerData *sd, void *data)
{
	double *accum = (double*) data;
	printf("Average temperature: %lf\n", *accum / sd->sample);
	free(accum);
}
Sampler averageTemperatureSampler(void)
{
	Sampler sampler;
	sampler.samplerConf = NULL;
	sampler.start  = &avgTempStart;
	sampler.sample = &avgTempSample;
	sampler.stop   = &avgTempStop;
	sampler.header = "# Average temperature:\n";
	return sampler;
}




/* STATS / VERBOSE */

static SamplerSignal dumpStatsSample(SamplerData *sd, void *data)
{
	UNUSED(sd);
	UNUSED(data);
	dumpStats();
	return SAMPLER_OK;
}
Sampler dumpStatsSampler(void)
{
	Sampler sampler;
	sampler.samplerConf = NULL;
	sampler.start  = NULL;
	sampler.sample = &dumpStatsSample;
	sampler.stop   = NULL;
	sampler.header = NULL;
	return sampler;
}



/* POSITION / CENTER OF MASS */

typedef struct
{
	Particle *ps;
	int num;
} ParticlesCOMSamplerConf;
static SamplerSignal particlesCOMSample(SamplerData *sd, void *data)
{
	UNUSED(sd);
	ParticlesCOMSamplerConf *pcsc = (ParticlesCOMSamplerConf*) data;
	Vec3 COM = getCOM(pcsc->ps, pcsc->num);
	printf("%le ", getTime());
	printVectorExp(COM);
	printf("\n");
	return SAMPLER_OK;
}
static Sampler particlesCOMSampler(Particle *ps, int num)
{
	ParticlesCOMSamplerConf *pcsc = malloc(sizeof(*pcsc));
	memset(pcsc, 0, sizeof(*pcsc));
	pcsc->ps = ps;
	pcsc->num = num;

	Sampler sampler;
	sampler.samplerConf = pcsc;
	sampler.start  = &passConf;
	sampler.sample = &particlesCOMSample;
	sampler.stop   = &freeState;
	sampler.header = "# <time> <position vector of COM>\n";
	return sampler;
}

Sampler particlePositionSampler(Particle *p)
{
	return particlesCOMSampler(p, 1);
}
Sampler strandCOMSampler(Strand *s)
{
	return particlesCOMSampler(s->all, 3 * s->numMonomers);
}



/* SQUARED DISPLACEMENT */

typedef struct
{
	Particle *ps;
	int num;
	Vec3 initialPos;
	double initialTime;
} SquaredDisplacementConf;

static void *particlesSquaredDisplacementStart(SamplerData *sd, void *conf)
{
	UNUSED(sd);
	SquaredDisplacementConf *sdc = (SquaredDisplacementConf*) conf;
	sdc->initialPos = getCOM(sdc->ps, sdc->num);
	sdc->initialTime = getTime();
	return sdc;
}
static SamplerSignal particlesSquaredDisplacementSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	SquaredDisplacementConf *sdc = (SquaredDisplacementConf*) state;

	Vec3 COM = getCOM(sdc->ps, sdc->num);
	Vec3 displacement = sub(COM, sdc->initialPos);
	double squaredDisplacement = length2(displacement);
	//printf("%le %le", getTime() - sdc->initialTime, squaredDisplacement);
	printf("%le ", getTime());
	printVectorExp(COM);
	printf("\n");

	if (squaredDisplacement > 0.25 * SQUARE(world.worldSize)) {
		fprintf(stderr, "\nWARNING! displacement > 0.5*worldsize. "
				"Probably errors due to periodic boundary "
				"conditions! Bailing out!\n");
		return SAMPLER_ERROR;
	}

	return SAMPLER_OK;
}
static Sampler particlesSquaredDisplacementSampler(Particle *ps, int num)
{
	SquaredDisplacementConf *sdc = malloc(sizeof(*sdc));
	memset(sdc, 0, sizeof(*sdc));
	sdc->ps = ps;
	sdc->num = num;

	Sampler sampler;
	sampler.samplerConf = sdc;
	sampler.start  = &particlesSquaredDisplacementStart;
	sampler.sample = &particlesSquaredDisplacementSample;
	sampler.stop   = &freeState;
	sampler.header = "# <time> <position vector of COM>\n";
	return sampler;
}

Sampler particleSquaredDisplacementSampler(Particle *p)
{
	return particlesSquaredDisplacementSampler(p, 1);
}
Sampler strandCOMSquaredDisplacementSampler(Strand *s)
{
	return particlesSquaredDisplacementSampler(s->all, 3 * s->numMonomers);
}



/* END TO END DISTANCE */

typedef struct
{
	Strand *strand;
} EndToEndDistConf;

static SamplerSignal endToEndDistSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	EndToEndDistConf *etedc = (EndToEndDistConf*) state;
	Strand *s = etedc->strand;

	Vec3 ete = endToEndVector(s);

	printf("%le\t%le\t", getTime(), length(ete));
	printVectorExp(ete);
	printf("\n");

	return SAMPLER_OK;
}
Sampler endToEndDistSampler(Strand *strand)
{
	EndToEndDistConf *etedc = malloc(sizeof(*etedc));
	memset(etedc, 0, sizeof(*etedc));
	etedc->strand = strand;

	Sampler sampler;
	sampler.samplerConf = etedc;
	sampler.start  = &passConf;
	sampler.sample = &endToEndDistSample;
	sampler.stop   = &freeState;
	sampler.header = "# <time> <end-to-end distance> <x,y,z of end-to-end vector>\n";
	return sampler;
}





/* GYRATION RADIUS */
typedef struct
{
	Strand *strand;
} GyrationRadiusConf;
static SamplerSignal gyrationRadiusSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	GyrationRadiusConf *grc = (GyrationRadiusConf*) state;
	Strand *s = grc->strand;

	/* To get the mean position correctly, we need to undo the periodic boundary conditions! */
	undoPeriodicBoundaryConditions(s);

	Vec3 meanPosition = {0, 0, 0};
	for (int i = 0; i < 3*s->numMonomers; i++)
		meanPosition = add(meanPosition, s->all[i].pos);
	meanPosition = scale(meanPosition, 1.0/(3 * s->numMonomers));

	double gyrationRadius = 0;
	for (int i = 0; i < 3*s->numMonomers; i++)
		gyrationRadius += distance2(s->all[i].pos, meanPosition);
	gyrationRadius = sqrt(gyrationRadius / (3 * s->numMonomers));

	printf("%le\t%le\n", getTime(), gyrationRadius);

	redoPeriodicBoundaryConditions(s);

	return SAMPLER_OK;
}
Sampler gyrationRadiusSampler(Strand *strand)
{
	GyrationRadiusConf *grc = malloc(sizeof(*grc));
	memset(grc, 0, sizeof(*grc));
	grc->strand = strand;

	Sampler sampler;
	sampler.samplerConf = grc;
	sampler.start  = &passConf;
	sampler.sample = &gyrationRadiusSample;
	sampler.stop   = &freeState;
	sampler.header = "# <time> <gyration radius>\n";
	return sampler;
}





/* HAIRPIN FORMATION */
/* Print (half of) the hairpin config (bound or unbound base matching 
 * pairs) and return the number of correctly bound base pairs. */
static int dumpHairpinStateOnlyCorrect(Strand *s, double energyThreshold)
{
	int correctlyBound = 0;
	int n = s->numMonomers;
	for (int i = 0; i < n/2; i++) {
		int j = n - 1 - i;
		double V = VbasePair(&s->Bs[i], &s->Bs[j]);
		if (V < energyThreshold) {
			correctlyBound++;
			printf("1 ");
		} else {
			printf("0 ");
		}
	}
	return correctlyBound;
}
typedef struct {
	double bestEnergy;
	Particle *p;
} DumpHairpinStateFullData;
static void dumpHairpinStateFullWorker(Particle *p1, Particle *p2, void *data)
{
	DumpHairpinStateFullData *dhsfData = (DumpHairpinStateFullData*) data;
	double V = VbasePair(p1, p2);
	if (V >= dhsfData->bestEnergy)
		return;

	dhsfData->bestEnergy = V;
	dhsfData->p = p2;
}
static int dumpHairpinStateFull(Strand *s, double energyThreshold)
{
	int correctlyBound = 0;
	int n = s->numMonomers;
	DumpHairpinStateFullData dhsfData;
	for (int i = 0; i < n; i++) {
		dhsfData.bestEnergy = INFINITY;
		dhsfData.p = NULL;
		forEveryNeighbourOf(&s->Bs[i], &dumpHairpinStateFullWorker, 
								&dhsfData);
		int j = n - 1 - i; /* Matching base */
		if (dhsfData.p == NULL) {
			/* No particles nearby */
			printf("0 ");
		} else if (dhsfData.bestEnergy > energyThreshold) {
			/* No (sufficient) binding */
			printf("0 ");
		} else {
			if (dhsfData.p == &s->Bs[j]) {
				/* Correctly bound */
				printf("1 ");
				correctlyBound++;
			} else {
				/* Misbound */
				printf("2 ");
			}
		}

	}
	return correctlyBound;
}
static int dumpHairpinState(Strand *s, double energyThreshold)
{
	if (getInteractionSettings().basePairInteraction == BASE_PAIR_HAIRPIN)
		return dumpHairpinStateOnlyCorrect(s, energyThreshold);
	else
		return dumpHairpinStateFull(s, energyThreshold);
}

/* Print (half of) the hairpin config (energy of matching base pairings)
 * and return the number of correctly bound base pairs. */
static int dumpHairpinEnergyState(Strand *s, double energyThreshold)
{
	int correctlyBound = 0;
	int n = s->numMonomers;
	for (int i = 0; i < n/2; i++) {
		int j = n - 1 - i;
		double V = VbasePair(&s->Bs[i], &s->Bs[j]);
		printf("%le ", V);
		if (V < energyThreshold)
			correctlyBound++;
	}
	return correctlyBound;
}
/* Print (half of) the hairpin config (distance between base pairs). Note: 
 * watch out for periodic boundary conditions, which limit the maximum base 
 * pair length! */
static void dumpHairpinDistanceState(Strand *s)
{
	int n = s->numMonomers;
	for (int i = 0; i < n/2; i++) {
		int j = n - 1 - i;
		printf("%le ", nearestImageDistance(s->Bs[i].pos, s->Bs[j].pos));
	}
}
static int getCorrectlyBoundHairpinBasePairs(Strand *s, double energyThreshold)
{
	int correctlyBound = 0;
	int n = s->numMonomers;
	for (int i = 0; i < n/2; i++) {
		int j = n - 1 - i;
		double V = VbasePair(&s->Bs[i], &s->Bs[j]);
		if (V < energyThreshold)
			correctlyBound++;
	}
	return correctlyBound;
}

typedef struct {
	enum {
		WAITING_TO_ZIP,
		WAITING_FOR_ZIPPING_CONFIRMATION,
		RELAXATION_IN_ZIPPED_STATE,
		WAITING_TO_UNZIP,
		WAITING_FOR_UNZIPPING_CONFIRMATION,
		UNZIPPING_CONFIRMED,
	} status;
	double zippingPhaseStartTime;
	double unzippingPhaseStartTime;
	double confirmationStartTime; /* Used for both zipping & unzipping */
	double confirmationTime; /* Used for both zipping & unzipping */
	double relaxationEndTime; /* Time at which to stop relaxation phase */

	/* First time where there were more than the nucleation threshold 
	 * bound base pairs for the entire time up till now. Negative if no 
	 * nucleation happened, or if we dropped back down below the 
	 * nucleation threshold since the previous nucleation. */
	double nucleationTime;

	HairpinFormationSamplerConfig conf;
} HairpinFormationSamplerData;

static void* hairpinFormationStart(SamplerData *sd, void *conf)
{
	UNUSED(sd);
	HairpinFormationSamplerConfig *hfc =
			(HairpinFormationSamplerConfig*) conf;
	HairpinFormationSamplerData *hfd = malloc(sizeof(*hfd));
	memset(hfd, 0, sizeof(*hfd));

	int n = world.strands[0].numMonomers;
	if (n < 1)
		die("hairpinFormationSampler: no DNA strands in world!\n");

	hfd->conf = *hfc; /* struct copy */
	hfd->status = WAITING_TO_ZIP;
	hfd->zippingPhaseStartTime = getTime();
	hfd->nucleationTime = -1;

	/* Set temperature for zipping */
	setHeatBathTemperature(hfc->zippingTemperature);

	/* Dump info */
	octaveScalar("sampleStartTime",        getTime());
	octaveScalar("zippingPhaseStartTime",  getTime());
	octaveScalar("zippingTemperature",     hfc->zippingTemperature);
	octaveScalar("unzippingTemperature",   hfc->unzippingTemperature);
	octaveScalar("energyThreshold",        hfc->energyThreshold);
	octaveScalar("nucleationBoundBPs",     hfc->nucleationBoundBPs);
	octaveScalar("requiredBoundBPs",       hfc->requiredBoundBPs);
	octaveScalar("allowedBoundBPs",        hfc->allowedBoundBPs);
	octaveScalar("zipConfirmationTime",    hfc->zipConfirmationTime);
	octaveScalar("unzipConfirmationTime",  hfc->unzipConfirmationTime);
	octaveScalar("minZippingSamplingTime", hfc->minZippingSamplingTime);
	octaveScalar("numMonomers",            n);
	octaveScalar("IntegratorTimestep",     getIntegratorTimeStep());
	octaveScalar("sampleInterval",         sd->sampleInterval);

	return hfd;
}
static SamplerSignal hairpinFormationSample(SamplerData *sd, void *state)
{
	HairpinFormationSamplerData *hfd =
			(HairpinFormationSamplerData*) state;
	HairpinFormationSamplerConfig *hfc = &hfd->conf;

	int correctlyBound = getCorrectlyBoundHairpinBasePairs(
				&world.strands[0], hfc->energyThreshold);
	int requiredBounds = hfc->requiredBoundBPs; /* for zipping */
	int nucleationBounds = hfc->nucleationBoundBPs; /* for zipping */
	int allowedBounds = hfc->allowedBoundBPs; /* for unzipping */

	if(sd->string != NULL)
		snprintf(sd->string, sd->strBufSize,
				"Correct hairpin BPs: %d, thresholds: %d and %d",
				correctlyBound, allowedBounds, requiredBounds);

	double time = getTime();

	switch (hfd->status) {
	case WAITING_TO_ZIP:
		octaveStartComment();
		printf("[waiting to zip] %le %d ", time, correctlyBound);
		dumpHairpinState(&world.strands[0], hfc->energyThreshold);
		octaveEndComment();

		if (correctlyBound >= nucleationBounds) {
			if (hfd->nucleationTime < 0)
				hfd->nucleationTime = time;
		} else {
			hfd->nucleationTime = -1;
		}

		if (correctlyBound < requiredBounds)
			break; /* Keep waiting */
		
		/* We have detected initial zipping! */
		hfd->confirmationStartTime = time;
		hfd->status = WAITING_FOR_ZIPPING_CONFIRMATION;
		octaveComment("Reached zipping binding threshold of %d base "
				"pairs at %le after a time %le", requiredBounds,
				time, time - hfd->zippingPhaseStartTime);
		/* Intentional fall through */
	case WAITING_FOR_ZIPPING_CONFIRMATION:
		octaveStartComment();
		printf("[waiting to zip] %le %d ", time, correctlyBound);
		dumpHairpinState(&world.strands[0], hfc->energyThreshold);
		octaveEndComment();
		if (correctlyBound < requiredBounds) {
			/* It was a dud! */
			hfd->status = WAITING_TO_ZIP;
			octaveComment("Could not confirm zipping, waiting to "
					"zip again at %le", time);
			break;
		}
		if (time - hfd->confirmationStartTime
						< hfc->zipConfirmationTime)
			break; /* Need to wait for confirmation */

		/* We have zipping confirmation! */
		double timeTillZipping = time - hfd->zippingPhaseStartTime
						- hfc->zipConfirmationTime;

		double timeTillZippingFromNucl = time - hfd->nucleationTime
						- hfc->zipConfirmationTime;
		octaveComment("Confirmed zipping at %le", time);
		octaveScalar("timeTillZipping", timeTillZipping);
		octaveScalar("timeTillZippingFromNucl", timeTillZippingFromNucl);
		octaveScalar("nucleationTime", hfd->nucleationTime);
		octaveComment("Starting relaxation phase in zipped state "
				"at %le", time);
		if (timeTillZippingFromNucl + hfc->zippedRelaxationTime
						< hfc->minZippingSamplingTime) {
			hfd->relaxationEndTime = time +
					hfc->minZippingSamplingTime - timeTillZippingFromNucl;
			octaveComment("Extending zipped relaxation time to "
					"honor minZippingSamplingTime");
		} else {
			hfd->relaxationEndTime = time + hfc->zippedRelaxationTime;
		}
		hfd->confirmationTime = time;
		hfd->status = RELAXATION_IN_ZIPPED_STATE;

		writeWorld(hfc->zippedStateFile);

		/* Intentional fall through */
	case RELAXATION_IN_ZIPPED_STATE:
		octaveStartComment();
		printf("[zipped relaxation] %le %d ", time, correctlyBound);
		dumpHairpinState(&world.strands[0], hfc->energyThreshold);
		octaveEndComment();
		if (time < hfd->relaxationEndTime)
			break; /* Relax further */
		/* End of relaxation phase. Go to WAITING_TO_UNZIP if we 
		 * are still zipped, otherwise, wait until we are zipped 
		 * again! */
		if (correctlyBound < requiredBounds) {
			/* Not zipped! */
			octaveComment("Relaxation in zipped state: passed "
					"zippedRelaxationTime but not zipped "
					"anymore at: %le -- total relax time: %le",
					time, time - hfd->confirmationTime);
			break; /* Wait to fully zip again */
		}

		/* We are still zipped! Go to next phase. */
		setHeatBathTemperature(hfc->unzippingTemperature);
		octaveScalar("unzippingPhaseStartTime", time);
		hfd->unzippingPhaseStartTime = time;
		hfd->status = WAITING_TO_UNZIP;
		break;
	case WAITING_TO_UNZIP:
		if (correctlyBound > allowedBounds) {
			/* Not yet unzipped. Just print info and continue. */
			octaveStartComment();
			printf("[waiting to unzip] %le %d ", time, correctlyBound);
			dumpHairpinState(&world.strands[0], hfc->energyThreshold);
			octaveEndComment();
			break;
		}

		/* We have detected initial unzipping! */
		hfd->confirmationStartTime = time;
		hfd->status = WAITING_FOR_UNZIPPING_CONFIRMATION;
		octaveComment("Reached unzipping binding threshold of %d base "
				"pairs at %le after a time %le", allowedBounds,
				time, time - hfd->unzippingPhaseStartTime);
		/* Intentional case fall through
		 * (if hfc->unzipConfirmationTime == 0 [or smaller than 
		 * sample interval], we need to stop right now!) */
	case WAITING_FOR_UNZIPPING_CONFIRMATION:
		octaveStartComment();
		printf("[waiting to unzip] %le %d ", time, correctlyBound);
		dumpHairpinState(&world.strands[0], hfc->energyThreshold);
		octaveEndComment();
		if (correctlyBound > allowedBounds) {
			/* It was a dud! */
			hfd->status = WAITING_TO_UNZIP;
			octaveComment("Could not confirm unzipping, waiting "
					"to unzip again at %le", time);
			break;
		}
		if (time - hfd->confirmationStartTime
						< hfc->unzipConfirmationTime)
			break; /* need to wait for confirmation */

		/* We have unzipping confirmation! */
		hfd->status = UNZIPPING_CONFIRMED;
		double timeTillUnzipping = time - hfd->unzippingPhaseStartTime
						- hfc->unzipConfirmationTime;
		octaveComment("Confirmed unzipping at %le", time);
		octaveScalar("timeTillUnzipping", timeTillUnzipping);
		return SAMPLER_STOP;
	case UNZIPPING_CONFIRMED:
		/* This can happen if our SAMPLER_STOP request got blocked 
		 * because we need to sample longer. In that case, just 
		 * keep writing state and request to stop again. */
		octaveStartComment();
		printf("[after unzipping] %le %d ", time, correctlyBound);
		dumpHairpinState(&world.strands[0], hfc->energyThreshold);
		octaveEndComment();
		return SAMPLER_STOP;
	default:
		fprintf(stderr, "Unknown status in hairpinFormationSample!\n");
		assert(false); return SAMPLER_ERROR;
	}
	return SAMPLER_OK;
}
Sampler hairpinFormationSampler(HairpinFormationSamplerConfig *hfc)
{
	Sampler sampler;
	HairpinFormationSamplerConfig *hfcCopy = malloc(sizeof(*hfcCopy));
	memcpy(hfcCopy, hfc, sizeof(*hfcCopy));

	sampler.samplerConf = hfcCopy;
	sampler.start  = &hairpinFormationStart;
	sampler.sample = &hairpinFormationSample;
	sampler.stop   = &freeState;
	sampler.header = "# Hairpin formation measurement. This file is "
			"readable by Octave.\n";
	return sampler;
}




/* HAIRPIN STATE */
static void* hairpinStateStart(SamplerData *sd, void *conf)
{
	UNUSED(sd);
	HairpinStateSamplerConfig *hsc = (HairpinStateSamplerConfig*) conf;

	int n = world.strands[0].numMonomers;
	if (n < 1)
		die("hairpinStateSampler: no DNA strands in world!\n");

	/* Dump info */
	octaveComment("temperatureBeforeStart  %le", getHeatBathTemperature());
	octaveComment("energyThreshold %le",         hsc->energyThreshold);
	octaveComment("numMonomers %d",              n);
	octaveComment("integratorTimestep %le",      getIntegratorTimeStep());
	octaveComment("sampleInterval %le",          sd->sampleInterval);
	octaveComment("dumpDistances %d",            hsc->dumpDistances);

	/* Set temperature if necessary */
	if (hsc->temperature >= 0)
		setHeatBathTemperature(hsc->temperature);

	octaveComment("temperatureAfterStart  %le",  hsc->temperature);

	return hsc;
}
static SamplerSignal hairpinStateSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	HairpinStateSamplerConfig *hsc = (HairpinStateSamplerConfig*) state;

	int correctlyBound = getCorrectlyBoundHairpinBasePairs(
				&world.strands[0], hsc->energyThreshold);

	printf("%le %d ", getTime(), correctlyBound);

	if (hsc->dumpDistances)
		dumpHairpinDistanceState(&world.strands[0]);
	else
		dumpHairpinState(&world.strands[0], hsc->energyThreshold);

	printf("\n");

	return SAMPLER_OK;
}
Sampler hairpinStateSampler(HairpinStateSamplerConfig *hsc)
{
	Sampler sampler;
	HairpinStateSamplerConfig *hscCopy = malloc(sizeof(*hscCopy));
	memcpy(hscCopy, hsc, sizeof(*hscCopy));

	sampler.samplerConf = hscCopy;
	sampler.start  = &hairpinStateStart;
	sampler.sample = &hairpinStateSample;
	sampler.stop   = &freeState;
	sampler.header = "# Hairpin state measurement. This file is "
			"readable by Octave.\n";

	return sampler;
}




/* HAIRPIN CONFIG SAMPLER */
typedef struct {
	Strand *s;
	char *configFilePrefix;
	double energyThreshold;
	int maxPrevBound;
} ConfigSamplerData;
static SamplerSignal hairpinConfigSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	ConfigSamplerData *csd = (ConfigSamplerData*) state;

	int numBound = getCorrectlyBoundHairpinBasePairs(csd->s, 
						csd->energyThreshold);
	if (numBound <= csd->maxPrevBound)
		return SAMPLER_OK;

	csd->maxPrevBound = numBound;
	char *configFile = asprintfOrDie("%s%d", csd->configFilePrefix, numBound);
	writeWorld(configFile);
	free(configFile);

	printf("%d\t%le\n", numBound, getTime());

	return SAMPLER_OK;
}
Sampler hairpinConfigSampler(HairpinConfigSamplerConfig *hcsc)
{
	Sampler sampler;
	ConfigSamplerData *csd = malloc(sizeof(*csd));
	csd->s = hcsc->s;
	csd->energyThreshold = hcsc->energyThreshold;
	csd->configFilePrefix = hcsc->configFilePrefix;
	csd->maxPrevBound = 0;

	sampler.samplerConf = csd;
	sampler.start  = &passConf;
	sampler.sample = &hairpinConfigSample;
	sampler.stop   = &freeState;
	sampler.header = "# Hairpin config measurement. This file holds "
			"extra data that accompany the dumped world "
			"configurations.\n"
			"# <number of bound base pairs == config file suffix> <time>\n";
	return sampler;
}




/* HAIRPIN MELTING TEMPERATURE */
typedef struct {
	enum {
		START_RELAXATION,
		WAITING_TO_RELAX,
		MEASURING,
	} status;
	double relaxStartTime;
	double measureStartTime;
	int measureIteration;
	int numMeasureIterations;
	int currentStep;
	long accumulatedBoundBasePairs;
	double *averageBPsPerStep;
	HairpinMeltingTempSamplerConfig conf;
} HairpinMeltingTempSamplerData;

static void* hairpinMeltingTempStart(SamplerData *sd, void *conf)
{
	HairpinMeltingTempSamplerConfig *hmtc =
				(HairpinMeltingTempSamplerConfig*) conf;
	HairpinMeltingTempSamplerData *hmtd = malloc(sizeof(*hmtd));
	memset(hmtd, 0, sizeof(*hmtd));

	hmtd->conf = *hmtc; /* struct copy */
	hmtd->status = START_RELAXATION;
	hmtd->currentStep = 0;

	int n = world.strands[0].numMonomers;
	if (n < 1)
		die("hairpinMeltingTempSampler: no DNA strands in world!\n");

	/* Set this once here, so all steps have the exact same number of 
	 * iterations. It's a PITA to do data processing otherwise. */
	hmtd->numMeasureIterations = hmtc->measureTime / sd->sampleInterval;

	/* We need to accumulate these and only dump them at the end, 
	 * because we cannot interfere with the matrix stream that we dump 
	 * when 'hmtc->verbose' is true. */
	hmtd->averageBPsPerStep = calloc(sizeof(*hmtd->averageBPsPerStep), 
					hmtc->numSteps);
	octaveScalar("sampleStartTime",    getTime());
	octaveScalar("temperature",        getHeatBathTemperature());
	octaveScalar("relaxationTime",     hmtc->relaxationTime);
	octaveScalar("measureTime",        hmtc->measureTime);
	octaveScalar("numMonomers",        n);
	octaveScalar("IntegratorTimestep", getIntegratorTimeStep());
	octaveScalar("sampleInterval",     sd->sampleInterval);

	octaveMatrixHeader("temperatures", hmtc->numSteps, 1);
	for (int i = 0; i < hmtc->numSteps; i++)
		printf("%le\n", hmtc->Tstart + i * hmtc->Tstep);

	if (hmtc->verbose) {
		/* Start the header for the 3D matrix of data */
		octaveComment("Series of 2D matrices of the form");
		octaveComment("data(:,:,i)' =");
		octaveComment(" [  time(t1) = t1      time(t2) = t2     ...   time(tn) = tn");
		octaveComment("    numBounds(t1)      numBounds(t2)     ...   numBounds(tn)");
		octaveComment("   lastBasePair(t1)   lastBasePair(t2)   ...  lastBasePair(tn)");
		octaveComment("         ...               ...           ...         ...      ");
		octaveComment("   firstBasePair(tn)  firstBasePair(t2)  ...  firstBasePair(tn) ]");
		octaveComment("Each such 2D matrix i is measured with the corresponding");
		octaveComment("temperature in the array 'temperatures': temperatures(i).");
		octave3DMatrixHeader("hairpinState",
				2 + n/2,
				hmtd->numMeasureIterations,
				hmtc->numSteps);
	}

	return hmtd;
}
static SamplerSignal hairpinMeltingTempSample(SamplerData *sd, void *state)
{
	HairpinMeltingTempSamplerData *hmtd = (HairpinMeltingTempSamplerData*) state;
	HairpinMeltingTempSamplerConfig *hmtc = &hmtd->conf;

	int correctlyBound = getCorrectlyBoundHairpinBasePairs(
				&world.strands[0], hmtc->energyThreshold);

	if(sd->string != NULL)
		snprintf(sd->string, sd->strBufSize,
				"Correct hairpin BPs: %d", correctlyBound);

	double time = getTime();

	switch (hmtd->status) {
	case START_RELAXATION:
		hmtd->relaxStartTime = time;
		hmtd->status = WAITING_TO_RELAX;
		/* Set temperature */
		setHeatBathTemperature(hmtc->Tstart
				+ hmtd->currentStep * hmtc->Tstep);
		break;
	case WAITING_TO_RELAX:
		if (time - hmtd->relaxStartTime < hmtc->relaxationTime)
			break;
		/* Relaxation done */
		hmtd->measureStartTime = time;
		hmtd->measureIteration = 0;
		hmtd->accumulatedBoundBasePairs = 0;
		hmtd->status = MEASURING;
		/* Intentional fall through */
	case MEASURING:
		hmtd->measureIteration++;
		if (hmtd->measureIteration > hmtd->numMeasureIterations) {
			/* End of this measurement run */
			hmtd->averageBPsPerStep[hmtd->currentStep] = 
				((double) hmtd->accumulatedBoundBasePairs)
					/ (double) hmtd->numMeasureIterations;

			hmtd->currentStep++;
			if (hmtd->currentStep >= hmtc->numSteps) {
				/* We are finished! Don't forget to dump 
				 * the accumulated bound base pairs! */
				octaveMatrixHeader("averageBoundBasePairs", 
							hmtc->numSteps, 1);
				for (int i = 0; i < hmtc->numSteps; i++)
					printf("%le\n", hmtd->averageBPsPerStep[i]);

				octaveComment("Successful end! :-)");
				return SAMPLER_STOP; /* All done! */
			}

			hmtd->status = START_RELAXATION;
			break;
		}

		/* Not at the end of this step yet: just sample */
		hmtd->accumulatedBoundBasePairs += correctlyBound;
		if (hmtc->verbose) {
			printf("%le ", time);
			dumpHairpinState(&world.strands[0], 
					hmtc->energyThreshold);
			printf("\n");
		}
		break;
	default:
		fprintf(stderr, "Unknown status in hairpinMeltingTempSample!\n");
		assert(false); return SAMPLER_ERROR;
	}
	return SAMPLER_OK;
}
Sampler hairpinMeltingTempSampler(HairpinMeltingTempSamplerConfig *hmtc)
{
	Sampler sampler;
	HairpinMeltingTempSamplerConfig *hmtcCopy = malloc(sizeof(*hmtcCopy));
	memcpy(hmtcCopy, hmtc, sizeof(*hmtcCopy));

	sampler.samplerConf = hmtcCopy;
	sampler.start  = &hairpinMeltingTempStart;
	sampler.sample = &hairpinMeltingTempSample;
	sampler.stop   = &freeState;
	sampler.header = "# Hairpin melting temperature measurement. This "
			"file is readable by Octave.\n";
	return sampler;
}




/* BASE PAIRING */

typedef struct
{
	int count;
	double threshold;
} BasePairingCounterData;
static int dumpDualStrandState(Strand *s1, Strand *s2, double energyThreshold)
{
	assert(s1->numMonomers == s2->numMonomers);
	int n = s1->numMonomers;
	int correctlyBound = 0;
	for (int i = 0; i < n; i++) {
		int j = n - 1 - i; //TODO
		double V = VbasePair(&s1->Bs[i],
				     &s2->Bs[j]);
		if (V < energyThreshold) {
			correctlyBound++;
			printf("1 ");
		} else {
			printf("0 ");
		}
	}
	return correctlyBound;
}
static void basePairingCounter(Particle *p1, Particle *p2, void *data)
{
	BasePairingCounterData *bpcd = (BasePairingCounterData*) data;
	double V = VbasePair(p1, p2);
	if (V < bpcd->threshold)
		bpcd->count++;
}
static SamplerSignal basePairingSample(SamplerData *sd, void *state)
{
	BasePairingConfig *bpc = (BasePairingConfig*) state;

	/* All base pairs */
	BasePairingCounterData bpcd;
	bpcd.count = 0;
	bpcd.threshold = bpc->energyThreshold;
	forEveryPairD(&basePairingCounter, &bpcd);

	/* Matching base pairs if two equal-length strands in the world 
	 * (assumed to be complementary) */
	int correctlyBound = -1; /* guard */
	if (world.numStrands == 2  &&  (world.strands[0].numMonomers
					== world.strands[1].numMonomers)) {
		correctlyBound = dumpDualStrandState(&world.strands[0], 
				&world.strands[1], bpc->energyThreshold);
	}
	/* Matching base pairs if one strands in the world (ASSUMED TO BE A 
	 * HAIRPIN!) -- TODO define via the mode enum in the config? */
	if (world.numStrands == 1) {
		switch (bpc->mode) {
		case BPSM_BOOLEAN:
			correctlyBound = dumpHairpinState(&world.strands[0], 
					bpc->energyThreshold);
			break;
		case BPSM_DISTANCE:
			correctlyBound = getCorrectlyBoundHairpinBasePairs(
					&world.strands[0], bpc->energyThreshold);
			dumpHairpinDistanceState(&world.strands[0]);
			break;
		case BPSM_ENERGY:
			correctlyBound = dumpHairpinEnergyState(&world.strands[0], 
					bpc->energyThreshold);
			break;
		}
	}

	if (correctlyBound >= 0) {
		printf("%le\t%d\t%d\n", getTime(), bpcd.count, correctlyBound);
		if(sd->string != NULL)
			snprintf(sd->string, sd->strBufSize,
					"All BPs: %d, Correct BPs: %d",
					bpcd.count, correctlyBound);
	} else {
		printf("%le\t%d\n", getTime(), bpcd.count);
		if(sd->string != NULL)
			snprintf(sd->string, sd->strBufSize,
					"All BPs: %d", bpcd.count);
	}

	return SAMPLER_OK;
}
Sampler basePairingSampler(BasePairingConfig *bpc)
{
	Sampler sampler;
	BasePairingConfig *bpcCopy = malloc(sizeof(*bpcCopy));
	memcpy(bpcCopy, bpc, sizeof(*bpcCopy));

	sampler.samplerConf = bpcCopy;
	sampler.start  = &passConf;
	sampler.sample = &basePairingSample;
	sampler.stop   = &freeState;
	sampler.header = "# <base pairs state> <time> <# bound BPs> "
						"[# correctly bound BPs]\n";
	return sampler;
}



/* FORCE, VELOCITY, FRICTION SAMPLER */
typedef struct
{
	Strand *strand;
} ForceVelFricConf;
static SamplerSignal forceVelFricSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	ForceVelFricConf *fvfc = (ForceVelFricConf*) state;
	Strand *s = fvfc->strand;

	printf("%le", getTime());

	for (int i = 0; i < s->numMonomers; i++) {
		Vec3 F = getMonomerForce(s, i);
		Vec3 v = getMonomerVelocity(s, i);
		printf("\t%le %le %le", 
				length(F), length(v), dot(F,v)/length2(F));
	}
	printf("\n");

	return SAMPLER_OK;
}
Sampler forceVelFricSampler(Strand *strand)
{
	ForceVelFricConf *fvfc = malloc(sizeof(*fvfc));
	memset(fvfc, 0, sizeof(*fvfc));
	fvfc->strand = strand;

	Sampler sampler;
	sampler.samplerConf = fvfc;
	sampler.start  = &passConf;
	sampler.sample = &forceVelFricSample;
	sampler.stop   = &freeState;
	sampler.header = "# <time> {for the COM of each monomer: <force |F|> <velocity |v|> <(F dot v) / |F|^2>}\n";
	return sampler;
}

/* similar, but sample on the backbone phosphates */
static SamplerSignal forceVelFricPSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	ForceVelFricConf *fvfc = (ForceVelFricConf*) state;
	Strand *s = fvfc->strand;

	printf("%le", getTime());

	for (int i = 0; i < s->numMonomers; i++) {
		Vec3 F = s->Ps[i].F;
		Vec3 v = s->Ps[i].vel;
		printf("\t%le %le %le", 
				length(F), length(v), dot(F,v)/length2(F));
	}
	printf("\n");

	return SAMPLER_OK;
}
Sampler forceVelFricPSampler(Strand *strand)
{
	ForceVelFricConf *fvfc = malloc(sizeof(*fvfc));
	memset(fvfc, 0, sizeof(*fvfc));
	fvfc->strand = strand;

	Sampler sampler;
	sampler.samplerConf = fvfc;
	sampler.start  = &passConf;
	sampler.sample = &forceVelFricPSample;
	sampler.stop   = &freeState;
	sampler.header = "# <time> {for the phosphate of each monomer: <force |F|> <velocity |v|> <(F dot v) / |F|^2>}\n";
	return sampler;
}

/* FORCE VELOCITY SAMPLER OF PHOSPHATE RELATIVE TO BACKBONE */
static SamplerSignal forceVelPSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	ForceVelFricConf *fvfc = (ForceVelFricConf*) state;
	Strand *s = fvfc->strand;

	printf("%le", getTime());

	for (int i = 0; i < s->numMonomers - 1; i++) {
		Vec3 F = s->Ps[i].F;
		Vec3 v = s->Ps[i].vel;
		Vec3 d = nearestImageUnitVector(s->Ss[i].pos, s->Ss[i+1].pos);
		double Fpara = dot(F, d);
		double vpara = dot(v, d);
		double Fperp = length(sub(F, scale(d, Fpara)));
		double vperp = length(sub(v, scale(d, vpara)));
		printf("\t%le %le %le %le", Fpara, Fperp, vpara, vperp);

		assert(equalsEpsilon(length2(F), SQUARE(Fpara) + SQUARE(Fperp), 1e-5));
		assert(equalsEpsilon(length2(v), SQUARE(vpara) + SQUARE(vperp), 1e-5));
	}
	printf("\n");

	return SAMPLER_OK;
}
Sampler forceVelPSampler(Strand *strand)
{
	ForceVelFricConf *fvfc = malloc(sizeof(*fvfc));
	memset(fvfc, 0, sizeof(*fvfc));
	fvfc->strand = strand;

	Sampler sampler;
	sampler.samplerConf = fvfc;
	sampler.start  = &passConf;
	sampler.sample = &forceVelPSample;
	sampler.stop   = &freeState;
	sampler.header = "#  F_para : component of force parallel to the backbone\n"
	                 "# |F_perp|: component of force perpendicular to the backbone\n"
	                 "#  v_para : component of velocity parallel to the backbone\n"
	                 "# |v_perp|: component of velocity perpendicular to the backbone\n"
	                 "# The direction of the backbone is defined as the vector linking the surrounding sugars of the phosphate, in the direction of increasing monomer number.\n"
	                 "# This means that the last monomer has no data (there is no sugar beyond its phosphate)\n"
	                 "# <time> {for the phosphate of each monomer except the last: <F_para> <|F_perp|> <v_para> <|v_perp|>}\n";
	return sampler;
}

/* FORCE VELOCITY SAMPLER OF SUGAR RELATIVE TO BACKBONE AND BASE */
static SamplerSignal forceVelSSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	ForceVelFricConf *fvfc = (ForceVelFricConf*) state;
	Strand *s = fvfc->strand;

	printf("%le", getTime());

	for (int i = 1; i < s->numMonomers; i++) {
		Vec3 F = s->Ps[i].F;
		Vec3 v = s->Ps[i].vel;
		Vec3 bb, base, perp;
		bb = nearestImageUnitVector(s->Ss[i-1].pos, s->Ss[i].pos);
		base = nearestImageVector(s->Ss[i].pos, s->Bs[i].pos);
		base = normalize(sub(base, scale(bb, dot(base,bb))));
		perp = cross(bb, base);

		printf("\t%le %le %le %le %le %le",
				dot(F, bb), dot(F, base), dot(F, perp),
				dot(v, bb), dot(v, base), dot(v, perp));

		assert(fabs(cosAngle(bb, base)) < 1e-5);
		assert(fabs(cosAngle(bb, perp)) < 1e-5);
		assert(fabs(cosAngle(perp, base)) < 1e-5);
		assert(equalsEpsilon(length(bb), 1, 1e-5));
		assert(equalsEpsilon(length(base), 1, 1e-5));
		assert(equalsEpsilon(length(perp), 1, 1e-5));
	}
	printf("\n");

	return SAMPLER_OK;
}
Sampler forceVelSSampler(Strand *strand)
{
	ForceVelFricConf *fvfc = malloc(sizeof(*fvfc));
	memset(fvfc, 0, sizeof(*fvfc));
	fvfc->strand = strand;

	Sampler sampler;
	sampler.samplerConf = fvfc;
	sampler.start  = &passConf;
	sampler.sample = &forceVelSSample;
	sampler.stop   = &freeState;
	sampler.header = "# F_bb:   component of force parallel to the backbone\n"
	                 "# F_base: component of force perpendicular to the backbone in the direction of the base\n"
	                 "# F_perp: component of force perpendicular to the backbone in the other perpendicular direction\n"
	                 "# v_*: analogous for the velocity\n"
	                 "# The 'bb' direction of the backbone is defined as the vector linking the surrounding phosphates of the sugar, in the direction of increasing monomer number.\n"
	                 "# The 'base' direction perpendiculal to the backbone is defined as the component perpendicular to bb of the vector pointing from the sugar to its base.\n"
	                 "# The third perpendicular direction is the cross product of 'bb' with 'base'.\n"
	                 "# This means that the first monomer has no data (there is no phosphate before its sugar)\n"
	                 "# <time> {for the sugar of each monomer except the first: <F_bb> <F_base> <F_perp> <v_bb> <v_base> <v_perp>}\n";
	return sampler;
}




/* TRIVIAL SAMPLER */

Sampler trivialSampler(void) {
	Sampler sampler = {
			.samplerConf = NULL,
			.start = NULL,
			.sample = NULL,
			.stop = NULL,
			.header = NULL,
	};
	return sampler;
}

/* TRIVIAL STOPPING SAMPLER */

static SamplerSignal trivialStoppingSample(SamplerData *sd, void *state)
{
	UNUSED(sd);
	double *stopTime = (double*) state;
	return (getTime() > *stopTime ? SAMPLER_STOP : SAMPLER_OK);
}
Sampler trivialStoppingSampler(double stopTime)
{
	double *conf = malloc(sizeof(*conf));
	*conf = stopTime;
	Sampler sampler = {
			.samplerConf = conf,
			.start = &passConf,
			.sample = &trivialStoppingSample,
			.stop = &freeState,
			.header = NULL,
	};
	return sampler;
}

