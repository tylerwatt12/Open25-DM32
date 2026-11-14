#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Classic saved-channel and real radio-state-machine regression tests."""
from pathlib import Path
import subprocess
import shlex
import sys
import os
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'build/radiofoundry-p25'
OUT=ROOT/'build/classic-host'

def run(args, **kw):
    return subprocess.run([str(a) for a in args],check=True,**kw)

def main():
    run(['python3',ROOT/'tools/build_radiofoundry.py','--prepare-only'],cwd=ROOT)
    OUT.mkdir(parents=True,exist_ok=True)
    host=APP/'tests/host'
    flags=['-std=gnu99','-Wall','-Wextra','-Wno-unused-parameter','-g',
           '-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-fno-omit-frame-pointer',
           '-I'+str(APP/'core'),'-I'+str(APP/'drivers'),'-I'+str(APP/'hal'),'-I'+str(host),
           '-D__attribute__(x)=','-DCONFIG_P25_CHANNELS=1','-DCONFIG_P25_NATIVE=1']
    run(['clang',*flags,'-ffunction-sections','-fdata-sections','-DCONFIG_DEV_TX=1',
         '-DCONFIG_P25_BENCH=0','-I'+str(ROOT/'tests/audio/stubs'),
         '-I'+str(APP/'core/p25'),ROOT/'tests/classic/test_p25_audio.c',
         APP/'core/p25/audio_fifo.c',APP/'core/radio/beep.c',
         '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
         '-o',OUT/'test_p25_audio'],cwd=ROOT)
    run([OUT/'test_p25_audio'],cwd=ROOT)
    run(['clang',*flags,ROOT/'tests/classic/test_channels.c',APP/'core/codeplug/codeplug.c',
         APP/'core/storage/storage.c',host/'sim/sim_flash.c','-o',OUT/'test_channels'],cwd=ROOT)
    run([OUT/'test_channels'],cwd=ROOT)
    # Link the actual UI storage job to the actual settings/codeplug/journal owners.
    run(['clang',*flags,'-ffunction-sections','-fdata-sections',
         '-DCONFIG_ZONE_EDIT=0','-DCONFIG_CONTACT_EDIT=0','-DCONFIG_KEY_ASSIGN=0',
         ROOT/'tests/classic/test_start_new.c',APP/'core/ui/ui_svc_storage.c',
         APP/'core/codeplug/codeplug.c',APP/'core/settings/settings.c',
         APP/'core/storage/storage.c',host/'sim/sim_flash.c',
         '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
         '-o',OUT/'test_start_new'],cwd=ROOT)
    run([OUT/'test_start_new'],cwd=ROOT)
    if os.environ.get('DM32_CLASSIC_BACKUP'):
        # Optional private regression input; never embedded in source or test fixtures.
        with tempfile.TemporaryDirectory(dir=OUT) as scratch:
            data=Path(scratch)/'data.bin'
            with zipfile.ZipFile(os.environ['DM32_CLASSIC_BACKUP']) as archive:
                data.write_bytes(archive.read('data.bin'))
            run([OUT/'test_start_new',data],cwd=ROOT)
    # Run the existing comprehensive FM/DMR suite against the transformed core.
    run(['make','-B','fd6818Test','SANITIZE=1'],cwd=host)
    run([host/'fd6818Test'],cwd=host)
    run(['make','-B','hrc7000DmrTest','SANITIZE=1'],cwd=host)
    run([host/'hrc7000DmrTest'],cwd=host)
    run(['make','-B','radioCoreTest','SANITIZE=1'],cwd=host)
    run([host/'radioCoreTest'],cwd=host)
    fixture=(host/'radioCoreTest.c').read_text().replace('int main(void)','int upstream_radio_main(void)')
    fixture=fixture.replace('const board_t board = {','const board_t board = {\n\t.native_modem=&classic_ops,')
    wrapper=OUT/'radioCoreClassic.c'
    wrapper.write_text('#include "native_modem/native_lab.h"\nextern const nt_ops classic_ops;\n'+fixture+
                       '\n#include "'+str(ROOT/'tests/classic/test_radio.inc')+'"\n')
    command=run(['make','-Bn','radioCoreTest','SANITIZE=1'],cwd=host,capture_output=True,text=True).stdout
    argv=shlex.split(next(line for line in command.splitlines() if ' -o radioCoreTest ' in line))
    argv[argv.index('radioCoreTest.c')]=str(wrapper)
    argv[argv.index('-o')+1]=str(OUT/'test_radio')
    argv+=['-I'+str(host),'-I'+str(ROOT/'tests/native/stubs'),'-I'+str(APP/'core'),
           '-I'+str(ROOT/'src/modem'),'-I'+str(ROOT/'integration/radiofoundry'),
           '-DCONFIG_P25_CHANNELS=1','-DCONFIG_P25_NATIVE=1','-DCONFIG_DEV_TX=1',
           str(ROOT/'src/modem/native_test.c'),str(ROOT/'integration/radiofoundry/native/native_lab.c')]
    run(argv,cwd=host)
    run([OUT/'test_radio'],cwd=host)
    command=run(['make','-Bn','uiFlowTest','SANITIZE=1'],cwd=host,capture_output=True,text=True).stdout
    argv=shlex.split(next(line for line in command.splitlines() if ' -o uiFlowTest ' in line))
    argv[argv.index('uiFlowTest.c')]=str(ROOT/'tests/classic/test_ui.c')
    argv[argv.index('-o')+1]=str(OUT/'test_ui')
    argv+=['-I'+str(host),'-DCONFIG_P25_CHANNELS=1']
    run(argv,cwd=host)
    run([OUT/'test_ui'],cwd=host)

if __name__=='__main__':main()
