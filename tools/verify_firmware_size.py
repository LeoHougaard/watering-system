"""Verify the built ESP32-S3 image and generated partitions; write CI evidence."""
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

FLASH_BYTES = 4 * 1024 * 1024
APP_BYTES = 1024 * 1024
IDF_REVISION = 'fcae32885b0296b32044cb99ecbdc50d98dddb83'


def verify_sizes(table, app_bytes, bootloader_bytes, table_bytes):
    table.verify()
    table.verify_size_fits(FLASH_BYTES)
    apps = [p for p in table if p.type == 0]
    if {(p.name, p.offset, p.size) for p in apps} != {
        ('factory', 0x20000, APP_BYTES), ('ota_0', 0x120000, APP_BYTES),
        ('ota_1', 0x220000, APP_BYTES),
    }:
        raise ValueError('Unexpected application partition layout')
    storage = next(p for p in table if p.name == 'storage')
    if (storage.offset, storage.size) != (0x320000, 896 * 1024):
        raise ValueError('Unexpected history storage partition')
    if not 0 < app_bytes <= min(p.size for p in apps):
        raise ValueError(f'Application image {app_bytes} bytes exceeds partition capacity')
    if not 0 < bootloader_bytes <= 0x8000:
        raise ValueError('Bootloader overlaps partition table at 0x8000')
    if not 0 < table_bytes <= 0x1000:
        raise ValueError('Partition table exceeds reserved sector')
    return [{'name': p.name, 'offset': p.offset, 'size_bytes': p.size,
             'free_bytes': p.size - app_bytes} for p in apps]


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    build = Path(sys.argv[1])
    idf = Path(os.environ['IDF_PATH'])
    sys.path.insert(0, str(idf/'components/partition_table'))
    from gen_esp32part import PartitionTable

    config = json.loads((build/'config/sdkconfig.json').read_text())
    expected = {'IDF_TARGET': 'esp32s3', 'ESPTOOLPY_FLASHSIZE': '4MB',
                'ESPTOOLPY_FLASHMODE': 'dio', 'ESPTOOLPY_FLASHFREQ': '80m',
                'PARTITION_TABLE_OFFSET': 0x8000, 'PARTITION_TABLE_CUSTOM': True,
                'SPIRAM': True, 'SPIRAM_MODE_QUAD': True,
                'COMPILER_OPTIMIZATION_SIZE': True, 'BOOTLOADER_APP_ROLLBACK_ENABLE': True}
    for key, value in expected.items():
        if config.get(key) != value:
            raise ValueError(f'Unexpected compiled setting {key}: {config.get(key)!r}')
    revision = command('git', '-C', str(idf), 'rev-parse', 'HEAD')
    if revision != IDF_REVISION:
        raise ValueError('ESP-IDF differs from the pinned project toolchain')
    app = build/'esp32_irrigation_controller.bin'
    bootloader = build/'bootloader/bootloader.bin'
    partition = build/'partition_table/partition-table.bin'
    table = PartitionTable.from_binary(partition.read_bytes())
    sizes = verify_sizes(table, app.stat().st_size, bootloader.stat().st_size, partition.stat().st_size)
    report = {'source_sha': command('git', 'rev-parse', 'HEAD'),
              'idf_revision': revision, 'idf_version': command('idf.py', '--version'),
              'compiler': command('xtensa-esp-elf-gcc', '--version').splitlines()[0],
              'container': os.environ.get('FIRMWARE_CONTAINER'),
              'input_sha256': {p: sha256(Path(p)) for p in ('sdkconfig.s3', 'sdkconfig.defaults', 'partitions.csv')},
              'compiled_config_sha256': sha256(build/'config/sdkconfig.json'),
              'board': 'Waveshare ESP32-S3-Zero / Zero-M', 'target': config['IDF_TARGET'],
              'flash_bytes': FLASH_BYTES, 'app_bytes': app.stat().st_size,
              'app_sha256': sha256(app), 'bootloader_bytes': bootloader.stat().st_size,
              'partition_table_bytes': partition.stat().st_size, 'app_partitions': sizes,
              'storage_bytes': 896 * 1024}
    (build/'firmware-evidence.json').write_text(json.dumps(report, indent=2)+'\n')
    summary = f"ESP32-S3 firmware: {report['app_bytes']:,} bytes; each application partition: {APP_BYTES:,} bytes; free: {APP_BYTES-report['app_bytes']:,} bytes. Source: {report['source_sha']}. ESP-IDF: {revision}."
    print(json.dumps(report, indent=2))
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as output:
            output.write(summary+'\n')


if __name__ == '__main__':
    main()
