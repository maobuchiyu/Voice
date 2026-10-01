"""生成固定 Flash 查表，公式和长度可审查；构建不依赖 Python。"""
import math
from pathlib import Path

target = Path(__file__).resolve().parents[1] / 'app' / 'signal_tables.h'
lines = ['/* 自动生成：1024 点正弦（含重复端点）和周期 Hann 窗。 */',
         '#ifndef ACOUSTICLAB_SIGNAL_TABLES_H', '#define ACOUSTICLAB_SIGNAL_TABLES_H']
for name, values in [('sine_table', [math.sin(2*math.pi*i/1024) for i in range(1025)]),
                     ('hann_table', [0.5*(1-math.cos(2*math.pi*i/2048)) for i in range(2048)])]:
    lines.append(f'static const float {name}[{len(values)}] = {{')
    for start in range(0, len(values), 8):
        lines.append('    ' + ', '.join(f'{v:.9e}f' for v in values[start:start+8]) + ',')
    lines.append('};')
lines.append('#endif')
target.write_text('\n'.join(lines)+'\n', encoding='utf-8')
