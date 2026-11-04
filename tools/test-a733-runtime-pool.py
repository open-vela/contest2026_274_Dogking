#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the actual allocator/reservations and client, with no NPU hardware.

The legacy 1 MiB profile must reproduce ENOMEM before submission. The optional
2 MiB profile must allocate/run/release real 39-tensor lengths repeatedly.
"""
import os
import re
import subprocess
import tempfile
from pathlib import Path


def function(source, name):
    matches = list(re.finditer(r'^static [^;{]*\b' + re.escape(name) + r'\([^;{]*\)\s*\{', source, re.M))
    if len(matches) != 1:
        raise ValueError('Ambiguous/missing current function: ' + name)
    start = matches[0].start(); end = matches[0].end(); depth = 1
    while depth:
        if source[end] == '{': depth += 1
        elif source[end] == '}': depth -= 1
        end += 1
    return source[start:end] + '\n'


def main():
    repo = Path(__file__).resolve().parent.parent
    overlay = repo / 'board/a733-cubie-a7z/openvela-overlay'
    chip = overlay / 'vendor/allwinnertech/chips/a733'
    source = (chip / 'a733_npu_runtime.c').read_text()
    app = (overlay / 'apps/system/aipet/voice_npu_check.cxx').read_text()
    constants = source[source.index('#define A733_RT_PAGE_SIZE'):source.index('struct a733_rt_context_s')]
    memory = (chip / 'a733_npu_memory.c').read_text()
    constants += re.search(r'#ifdef CONFIG_A733_NPU_GENERAL_ASR\n#define A733_NPU_ARENA_SIZE.*?\n#endif', memory, re.S).group() + '\n'
    structs = source[source.index('struct a733_rt_context_s'):source.index('struct a733_rt_a7pm_header_s')]
    native = re.search(r'static const uint32_t encoder_sizes\[\]=\{.*?\};', app, re.S).group()
    general = (overlay / 'apps/system/aipet/voice_general_contract.hxx').read_text()
    native += '\n' + re.search(r'static const uint32_t encoder\[\] = \{.*?\};', general, re.S).group().replace('encoder[]', 'general_encoder_sizes[]')
    reserved = re.search(r'  g_rt_pages\[0\] = true;[^\n]*', source).group() + '\n'
    reserved += re.search(r'  for \(page = 0; page < A733_RT_TRACE_WEIGHTS_PAGES; page\+\+\)\s*\{.*?\n    \}', source, re.S).group()
    funcs = ''.join(function(source, n) for n in ('a733_rt_find_buffer', 'a733_rt_free_bytes',
                                               'a733_rt_release_buffer', 'a733_rt_close_impl', 'a733_rt_alloc'))
    with tempfile.TemporaryDirectory(prefix='a733-real-pool-') as temp:
        root = Path(temp)
        for name, text in dict(constants=constants, structs=structs, native=native, functions=funcs,
                               reservations='unsigned page;\n' + reserved).items():
            (root / ('runtime-pool-' + name + '.inc')).write_text(text)
        for enabled, general in ((False,False),(True,False),(True,True)):
            binary = root / ('general' if general else 'enabled' if enabled else 'legacy')
            cmd = [os.environ.get('CC', 'gcc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-I' + str(root), '-I' + str(chip),
                   '-I' + str(repo / 'tools/tensor-mock'),
                   '-I' + str(overlay / 'apps/include'), '-I' + str(overlay / 'nuttx/include')]
            if enabled: cmd.append('-DCONFIG_A733_NPU_TENSOR_IO=1')
            if general: cmd.append('-DCONFIG_A733_NPU_GENERAL_ASR=1')
            sdk=Path(os.environ.get('OPENVELA_WORKSPACE',repo/'../../quickly-openvela'))
            subprocess.run(cmd + [str(repo / 'tools/test-a733-runtime-pool.c'),
                                 str(sdk/'nuttx/libs/libc/misc/lib_crc32.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
