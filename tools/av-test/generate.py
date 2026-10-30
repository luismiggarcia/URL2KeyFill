#!/usr/bin/env python3
"""Generate standalone layer assets. Run from any working directory."""
from pathlib import Path
import json
root = Path(__file__).resolve().parents[2]
template = Path(__file__).with_name('template.html').read_text()
tests = {
    'AV_Test': 'auto',
    '01_Barras_RGB': 'bars',
    '02_Escala_de_Cinzentos': 'gray',
    '03_Geometria_e_Movimento': 'grid',
    '04_Transparencia_Key': 'alpha',
    '05_Audio_Esquerdo': 'left',
    '06_Audio_Direito': 'right',
    '07_Audio_Estereo': 'stereo',
    '08_Audio_Oposicao_de_Fase': 'phase',
    '09_Audio_Varrimento': 'sweep',
    '10_Sincronismo_AV': 'sync',
    '11_Silencio_Digital': 'silence',
}
for filename, mode in tests.items():
    (root / 'assets/Diagnostics' / (filename + '.html')).write_text(
        template.replace('__MODE__', json.dumps(mode)))
print(f'{len(tests)} standalone assets generated.')
