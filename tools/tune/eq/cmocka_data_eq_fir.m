% Create a chirp waveform and export test EQ coefficients and reference output
%
% Usage:
% cmocka_data_eq_fir()

% SPDX-License-Identifier: BSD-3-Clause
%
% Copyright (c) 2021, Intel Corporation. All rights reserved.

function cmocka_data_eq_fir()

% Output files and paths
chirp_fn = '../../../test/cmocka/include/cmocka_chirp_2ch.h';
ref_fn = '../../../test/cmocka/src/audio/eq_fir/cmocka_fir_ref.h';
coef_fn = '../../../test/cmocka/src/audio/eq_fir/cmocka_fir_coef_2ch.h';

% Input data
fs = 48e3;
t = 100e-3;
scale = 2^31;
[x, yi] = get_chirp(fs, t);
export_c_int32t(chirp_fn, 'chirp_2ch', 'CHIRP_2CH_LENGTH',yi)

% Compute a test EQ
eq = test_response(coef_fn, 'fir_coef_2ch', fs);

% Filter input data
ref(:,1) = filter(eq.b_fir, 1, x(:,1));
ref(:,2) = filter(eq.b_fir, 1, x(:,2));
refi = scale_saturate(ref, scale);
export_c_int32t(ref_fn, 'fir_ref_2ch', 'FIR_REF_2CH_LENGTH', refi)

figure;
plot(yi/scale)
grid on;

figure;
plot(ref)
grid on;

figure;
plot(refi / scale)
grid on;

end


function xi = scale_saturate(x, scale)

imax = scale - 1;
imin = -scale;
xi = round(scale * x);
xi = min(xi, imax);
xi = max(xi, imin);

end

function [x, yi] = get_chirp(fs, t_chirp)

channels = 2;
f0 = 100;
f1 = 20e3;
a = 1 + 1e-5; % Ensure max and min int values are produced
scale = 2^31;
imax = scale - 1;
imin = -scale;

n = round(fs * t_chirp);
t = (0:(n - 1)) / fs;
x(:, 1) = a * chirp(t, f0, t_chirp, f1, 'logarithmic', 0);
x(:, 2) = a * chirp(t, f0, t_chirp, f1, 'logarithmic', 90);
x = min(x, 1.0);
x = max(x, -1.0);
yi = scale_saturate(x, scale);

end

%% -------------------
%% EQ design functions
%% -------------------

function eq = test_response(fn, vn, fs)

%% Get EQ
blob_fn = '../../ctl/ipc3/eq_fir_loudness.txt';
eq = eq_blob_plot(blob_fn, 'fir', fs, [], 0);

% Shorten the FIR to make it fit a single 384 byte IPC. The
% changed response does not matter in cmocka test case.
n_new = 100;
n_orig = length(eq.b_fir);
if n_orig > n_new
	n_center = floor(n_orig / 2);
	n_half = floor(n_new / 2);
	eq.b_fir = eq.b_fir(n_center - n_half:n_center + n_half - 1);
end

%% Quantize and pack filter coefficients plus shifts etc.
bq = eq_fir_blob_quant(eq.b_fir);

%% Build blob
channels_in_config = 2;  % Setup max 2 channels EQ
assign_response = [0 0]; % Same response for L and R
num_responses = 1;       % One response
bm = eq_fir_blob_merge(channels_in_config, ...
		       num_responses, ...
		       assign_response, ...
		       bq);

%% Pack and write file
bp = eq_fir_blob_pack(bm);
export_c_eq_uint32t(fn, bp, vn, 0);

end
