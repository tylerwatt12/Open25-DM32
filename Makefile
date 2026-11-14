.PHONY: radiofoundry sources toolchain protocol-test codec-test runtime-test voice-test native-test receive-audio-test classic-test test

radiofoundry:
	python3 tools/build_radiofoundry.py --classic --stack-check

sources:
	python3 tools/sources.py fetch

toolchain:
	sh tools/bootstrap_toolchain.sh

protocol-test:
	python3 tests/protocol/test_p25.py
	sh tests/protocol/run_sanitizers.sh

codec-test:
	python3 tools/test_codec_slot.py
	python3 experiments/codec/build.py --full-ubsan
	mkdir -p artifacts/codec-tests logs
	experiments/codec/out/ubsan/codec_harness --frames 2000 --fuzz 10000 --out-prefix artifacts/codec-tests/synthetic > logs/codec-tests.json
	experiments/codec/out/ubsan/codec_pipeline artifacts/codec-tests/synthetic.input.s16le artifacts/codec-tests/pipeline > logs/codec-pipeline.json

runtime-test:
	mkdir -p build/runtime
	cc -std=c11 -Os -fno-builtin -Dmemcpy=dm32_memcpy -Dmemset=dm32_memset -Dmemmove=dm32_memmove -fsanitize=address,undefined -c src/runtime/memory.c -o build/runtime/memory-host.o
	cc -std=c11 -Os -fsanitize=address,undefined tests/test_memory.c build/runtime/memory-host.o -o build/runtime/test_memory
	build/runtime/test_memory

voice-test:
	python3 tools/test_voice.py

native-test:
	python3 tools/test_native_modem.py

receive-audio-test:
	python3 tools/test_receive_audio.py

classic-test:
	python3 tools/test_classic.py

test: protocol-test codec-test runtime-test voice-test native-test receive-audio-test classic-test
