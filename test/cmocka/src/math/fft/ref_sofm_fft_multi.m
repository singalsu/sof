% ref_sofm_dft3 - Generate C header files for DFT3 function unit tests

% SPDX-License-Identifier: BSD-3-Clause
%
% Copyright(c) 2025 Intel Corporation. All rights reserved.

function ref_sofm_fft_multi()

	path(path(), '../../../m');
	opt.describe = export_get_git_describe();

	N = 1536;
	num_tests = 1;
	scale_q31 = 2^31;
	scale_q26 = 2^26;
	opt.bits = 32;

	if 0
		input_data_real_q31 = int32((2 * rand(N, num_tests) - 1) * scale_q31);
		input_data_imag_q31 = int32((2 * rand(N, num_tests) - 1) * scale_q31);
	end
	if 0
		input_data_real_q31 = int32(0.5 * ones(N, num_tests) * scale_q31);
		input_data_imag_q31 = int32(zeros(N, num_tests));
	end
	if 1
		ft = 997;
		fs = 48e3;
		t = (0:(N-1))'/fs;
		x = 0.5*sin(2*pi*ft*t) .* hamming(N);
		size(x)
		input_data_real_q31 = int32(x * scale_q31);
		input_data_imag_q31 = int32(zeros(N, num_tests));
	end

	input_data_real_f = double(input_data_real_q31) / scale_q31;
	input_data_imag_f = double(input_data_imag_q31) / scale_q31;
	input_data_f = complex(input_data_real_f, input_data_imag_f);
	save input_data_f.mat input_data_f

	ref_data_f = zeros(N, num_tests);
	for i = 1:num_tests
		ref_data_f(:,i) = fft(input_data_f(:,i));
	end

	input_data_vec_f = reshape(input_data_f, N * num_tests, 1);
	input_data_real_q31 = int32(real(input_data_vec_f) * scale_q31);
	input_data_imag_q31 = int32(imag(input_data_vec_f) * scale_q31);

	ref_data_vec_f = reshape(ref_data_f, N * num_tests, 1);
	ref_data_real_q26 = int32(real(ref_data_vec_f) * scale_q26);
	ref_data_imag_q26 = int32(imag(ref_data_vec_f) * scale_q26);

	header_fn = sprintf('ref_sofm_fft_multi_32.h');
	fh = export_headerfile_open(header_fn);
	comment = sprintf('Created %s with script ref_sofm_dft3.m %s', ...
			  datestr(now, 0), opt.describe);
	export_comment(fh, comment);
	export_ndefine(fh, 'REF_SOFM_FFT_MULTI_N', N);
	export_ndefine(fh, 'REF_SOFM_FFT_MULTI_NUM_TESTS', num_tests);
	export_vector(fh, opt.bits, 'input_data_real_q31', input_data_real_q31);
	export_vector(fh, opt.bits, 'input_data_imag_q31', input_data_imag_q31);
	export_vector(fh, opt.bits, 'ref_data_real_q26', ref_data_real_q26);
	export_vector(fh, opt.bits, 'ref_data_imag_q26', ref_data_imag_q26);
	fclose(fh);
	fprintf(1, 'Exported %s.\n', header_fn);
end
