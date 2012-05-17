function [timesTillZipping, temperature, temperatures, allData, measureTime, timestep, allowedUnboundBPs] = parseHairpins(filesglob, alsoLoadFullData)

if (nargin < 1)
	error("Not enough arguments!");
end
if (nargin == 1)
	alsoLoadFullData = false;
	allData = 0;
end

files = glob(filesglob);

nRuns = numel(files);

load(files{1}); % for constants, temperatures, etc
clear data;


%only load the actual data in a 4D array
for run = 1:nRuns
	timesTillZipping(run) = load(files{run}, "timeTillZipping").timeTillZipping;
	if (alsoLoadFullData)
		allData(:,:,:,run) = load(files{run}, "data").data;
	end
end

