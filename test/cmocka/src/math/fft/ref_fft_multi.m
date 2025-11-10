% ref_sofm_dft3 - Generate C header files for DFT3 function unit tests

% SPDX-License-Identifier: BSD-3-Clause
%
% Copyright(c) 2025 Intel Corporation. All rights reserved.

function ref_fft_multi()

    path(path(), '../../../m');
    opt.describe = export_get_git_describe();
    opt.bits = 32;
    opt.fs = 48e3;

    N = 16;
    make_fft_multi_test_vectors(opt, N);

    N = 96;
    make_fft_multi_test_vectors(opt, N);

    N = 512;
    make_fft_multi_test_vectors(opt, N);

    N = 768;
    make_fft_multi_test_vectors(opt, N);

    N = 1024;
    make_fft_multi_test_vectors(opt, N);

    N = 1536;
    make_fft_multi_test_vectors(opt, N);

    N = 3072;
    make_fft_multi_test_vectors(opt, N);

end

function make_fft_multi_test_vectors(opt, N)

    num_tests = 1;
    scale_q = 2^(opt.bits - 1);
    min_int = int32(-scale_q);
    max_int = int32(scale_q - 1);
    n = 1;

    input_data_real_q = int32(zeros(N, num_tests));
    input_data_imag_q = int32(zeros(N, num_tests));

    if 0
	input_data_real_q(:,n) = int32(ones(N, 1) * max_int);
	input_data_imag_q(:,n) = int32(ones(N, 1) * max_int);
        n = n + 1;
    end
    if 1
	ft = 997;
	t = (0:(N - 1))'/opt.fs;
	x = 10^(-1 / 20) * sin(2 * pi * ft * t) .* kaiser(N, 20);
	dither = scale_q / 2^19 * (rand(N, 1) + rand(N, 1) - 1);
	input_data_real_q(:,n) = int32(x * scale_q + dither);
        n = n + 1;
    end

    input_data_real_f = double(input_data_real_q) / scale_q;
    input_data_imag_f = double(input_data_imag_q) / scale_q;
    input_data_f = complex(input_data_real_f, input_data_imag_f);
    save input_data_f.mat input_data_f

    ref_data_f = zeros(N, num_tests);
    for i = 1:num_tests
	ref_data_f(:,i) = fft(input_data_f(:,i)) / N;
    end

    input_data_vec_f = reshape(input_data_f, N * num_tests, 1);
    input_data_real_q = int32(real(input_data_vec_f) * scale_q);
    input_data_imag_q = int32(imag(input_data_vec_f) * scale_q);

    ref_data_vec_f = reshape(ref_data_f, N * num_tests, 1);
    ref_data_real_q = int32(real(ref_data_vec_f) * scale_q);
    ref_data_imag_q = int32(imag(ref_data_vec_f) * scale_q);

    header_fn = sprintf('ref_fft_multi_%d_%d.h', N, opt.bits);
    fh = export_headerfile_open(header_fn);
    comment = sprintf('Created %s with script ref_sofm_dft3.m %s', ...
		      datestr(now, 0), opt.describe);
    export_comment(fh, comment);
    dstr = sprintf('REF_SOFM_FFT_MULTI_%d_NUM_TESTS', N);
    export_ndefine(fh, dstr, num_tests);
    qbits = opt.bits-1;
    vstr = sprintf('in_real_%d_q%d', N, qbits); export_vector(fh, opt.bits, vstr, input_data_real_q);
    vstr = sprintf('in_imag_%d_q%d', N, qbits); export_vector(fh, opt.bits, vstr, input_data_imag_q);
    vstr = sprintf('ref_real_%d_q%d', N, qbits); export_vector(fh, opt.bits, vstr, ref_data_real_q);
    vstr = sprintf('ref_imag_%d_q%d', N, qbits); export_vector(fh, opt.bits, vstr, ref_data_imag_q);
    fclose(fh);
    fprintf(1, 'Exported %s.\n', header_fn);
end
