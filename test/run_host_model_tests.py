"""Run the existing Unity model tests on Windows without flashing a test image."""
import os
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
idf = Path(os.environ.get('IDF_PATH', str(Path.home() / 'esp/v5.5.1/esp-idf')))
unity = idf / 'components/unity/unity/src'
cjson = idf / 'components/json/cJSON'
with tempfile.TemporaryDirectory(prefix='watering-model-') as directory:
    temp = Path(directory)
    source = (root / 'test/test_irrigation.c').read_text()
    names = []
    def test_case(match):
        name = f'test_{len(names)}'
        names.append(name)
        return 'void ' + name + '(void)'
    source = re.sub(r'TEST_CASE\("[^"]+",\s*"[^"]+"\)', test_case, source)
    source += '\nvoid setUp(void) {}\nvoid tearDown(void) {}\nint main(void) {UNITY_BEGIN();\n'
    source += '\n'.join('RUN_TEST(' + name + ');' for name in names)
    source += '\nreturn UNITY_END();}\n'
    (temp/'tests.c').write_text(source)
    subprocess.run(['gcc','-std=c11','-I'+str(unity),'-I'+str(cjson),
                    '-I'+str(root/'components/irrigation_model/include'),str(temp/'tests.c'),
                    str(unity/'unity.c'),str(cjson/'cJSON.c'),
                    str(root/'components/irrigation_model/irrigation_model.c'),
                    '-o',str(temp/'tests.exe')],check=True)
    subprocess.run([str(temp/'tests.exe')],check=True)
