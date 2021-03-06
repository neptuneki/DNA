% function [tau, tauStddev, offset, offsetStddev, amplitude, amplitudeStddev, time, meanEndToEndDist, errEndToEndDist] = averageEndToEndDist(filesglob, decimateFactor)
function [tau, tauStddev, offset, offsetStddev, amplitude, amplitudeStddev, time, meanEndToEndDist, errEndToEndDist] = averageEndToEndDist(filesglob, decimateFactor)

addpath("generic");

if (nargin < 1)
	error "Not enough required arguments given!"
elseif (nargin < 2)
	decimateFactor = 1;
end

[time, dists] = parseEndToEndDists(filesglob, decimateFactor);

% XXX
% This is a hack to make sure that all times are positive (for stretched 
% exponential fit). Due to the decimation, it is possible that we end up 
% with a tiny negative first time value.
time = time - time(1);


nRuns = numel(dists(1,:))
meanEndToEndDist = mean(dists')';
errEndToEndDist = std(dists')' / sqrt(nRuns - 1);

color = "b";
clf; hold on;
h1 = errorbar(time, meanEndToEndDist, errEndToEndDist);
set(h1, "color", color);
set(h1, "marker", ".");
set(h1, "linestyle", "none");
set(h1, "linewidth", 1);


initialLength = meanEndToEndDist(1); % This should have no error, as all strands have the same initial length!

[tau, offset, amplitude, tauStddev, offsetStddev, amplitudeStddev] = exponentialRelaxation(time, meanEndToEndDist, 4e-8, initialLength / 2, initialLength / 2, errEndToEndDist);
tauInfo = [tau, tauStddev]
offsetInfo = [offset, offsetStddev]
amplitudeInfo = [amplitude, amplitudeStddev]

fit = offset + amplitude * exp(-(time/tau));

plot(time, fit, "k", "linewidth", 4);
pause(1e-9);
hold off;
