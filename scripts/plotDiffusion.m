% datafilename: a file that holds a 3D matrix.
% data(:,:,i) = data of i'th run, which is:
%   [ time(1)  x(1)  y(1)  z(1);
%     time(2)  x(2)  y(2)  z(2);
%      ...     ...   ...   ...  ]
%
% It is assumed that the timedifference is the same everywhere (so the 
% first columns are pretty much redundant)
function [D, Derr, meanSqDisplacement, sqDisplacement] = plotDiffusion(datafilename, fraction)

if (nargin < 1)
	error("Not enough required arguments!");
end
if (nargin == 1)
	fraction = 0.5; %make every subtrajectory this fraction of the total simulation time
end

data = load(datafilename);
data = data.data;

nRuns = size(data)(3);

time = data(:,1,1);
dt = time(3) - time(2); %XXX HARDCODED FOR SAMPLING AT CONSTANT INTERVAL!
nSamples = numel(time);

nSamplesInResult = floor(nSamples * fraction);
stride = ceil(nSamples * 0.002);
nTrajectories = 1 + floor((nSamples - nSamplesInResult - 1) / stride); %number of sub trajectories

sqDisplacement = zeros(nSamplesInResult, nRuns);
for r = 1:nRuns
	positions = data(:, 2:4, r);
	for i = 1 : stride : (nSamples - nSamplesInResult)
		sqDisplacement(:,r) += squaredDisplacements(positions(i:i+nSamplesInResult, :));
	end
	sqDisplacement(:,r) /= nTrajectories;
end

ts = linspace(dt, nSamplesInResult*dt, nSamplesInResult)';
if (nRuns == 1)
	meanSqDisplacement = sqDisplacement;
	D = mean(meanSqDisplacement ./ (6 * ts))
else
	meanSqDisplacement = mean(sqDisplacement')';
	individualDs = sqDisplacement ./ (6 * (ts * ones(1,nRuns)));
	individualDstds = std(individualDs);
	individualMeanDs = mean(individualDs);
	D = mean(individualMeanDs);
	%TODO check error analysis
	statisticalErrBecauseOfIndividualStds = norm(individualDstds) / sqrt(nRuns)
	statisticalErrBecauseOfStdsInD = std(individualMeanDs)
	Derr = norm([statisticalErrBecauseOfStdsInD, statisticalErrBecauseOfIndividualStds]);
end

fit = 6*D*ts;

clf; hold on;
plot(ts, sqDisplacement)
plot(ts, fit, "k", "linewidth", 4);
plot(ts, meanSqDisplacement, "g", "linewidth", 4)
hold off;
