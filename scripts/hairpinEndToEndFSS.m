addpath generic

generate = true

T = 30;
Ns = [5, 6, 7, 8, 10, 12, 14, 17, 20, 24, 28, 34, 40, 48, 57, 67, 80];
T = 10;
Ns = [5, 6, 7, 8, 10, 12, 14, 17, 20, 24, 28, 34, 40, 48];
clusterSize = 2;
dropLogFactor = 3;
dataStartIndex = 2; % ignore first sample at 't = 0' which is actually at 't = dt'

opt.bootstrapSamples = 30;
opt.eps = 1e-5;
opt.fixOffsetToZero = true;
opt.singleExponent = false;
opt.twoTimescales = false;
opt.guessAlpha = 1;
opt.guessAlpha2 = 0.8;
opt.guessBeta = 1;
opt.guessEta = 1;
opt.delta = 2*0.587597; %because SQUARED deviation!
opt.squaredDeviation = true;
if opt.twoTimescales
	scalingFunction = @finiteSizeRescaleWithTwoTimes;
else
	scalingFunction = @finiteSizeRescaleWithTime;
end
opt.loglog = true;
opt.simulatedAnnealing = false;

filenamePrefix = ['./data/hairpinEndToEndT',num2str(T)];
variableName = 'dists';
resultFilePrefix = 'data/endToEndFSS';

if generate
[clustNs, clustPs, clustPErrs, clustQuals, clustQualErrs, opt] = hairpinFSSclustered(Ns, filenamePrefix, variableName, resultFilePrefix, clusterSize, dropLogFactor, scalingFunction, opt, dataStartIndex)
end

meanStartN = 20;
clf;
finiteSizeResultplots(resultFilePrefix, clusterSize, dropLogFactor, opt, meanStartN)
