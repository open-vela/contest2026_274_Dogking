#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only offline RAW/preprocessing audit and self-contained HTML report.

No network, inference, device access, auto-calibration, or firmware modification.
An offline-consistent bundle is NOT proof of capture origin/colour/accuracy.
"""
import argparse
from array import array
import base64
import hashlib
import html
import importlib.util
import json
from pathlib import Path
import struct
import sys
import zlib

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('vision201_report', HERE / 'a733-camera-vision-v201.py')
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)
PATTERNS = {'rggb': (0, 1, 1, 2), 'grbg': (1, 0, 2, 1),
            'gbrg': (1, 2, 0, 1), 'bggr': (2, 1, 1, 0)}
COLORS = ((0, 255, 102), (255, 204, 0), (0, 191, 255), (255, 85, 119))
CPU_SOURCE_SHA = 'cd80e7f75b9ef7f3ae3e2a9d59d3a9fae705c5c6efd318eacfdf854eac0da8a6'
EXPORT_SOURCE_SHA = 'f14496cc3babfe3c1ccead50dac9d125c43ff690e2473c24c0f9afcd942b4b77'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pixels(raw):
    v.require(len(raw) == 4147200, 'RAW size mismatch')
    values = array('H')
    values.frombytes(raw)
    if sys.byteorder != 'little':
        values.byteswap()
    v.require(max(values) <= 1023, 'RAW contains upper bits: expected right-justified RAW10 LE16')
    return values


def raw_stats(values):
    histogram = [0] * 1024
    parity = [0] * 4
    rows = [0] * 1080
    for y in range(1080):
        total = 0
        for x in range(1920):
            value = values[y * 1920 + x]
            histogram[value] += 1
            parity[(y % 2) * 2 + x % 2] += value
            total += value
        rows[y] = round(total / 1920, 4)
    size = len(values)
    occupied = [i for i, n in enumerate(histogram) if n]
    mean = sum(i * n for i, n in enumerate(histogram)) / size
    variance = sum((i - mean) ** 2 * n for i, n in enumerate(histogram)) / size
    warnings = []
    if len(occupied) == 1:
        warnings.append('UNIFORM_RAW: inspect source/capture; not an automatic hardware diagnosis')
    if histogram[0] / size > .95:
        warnings.append('MOSTLY_ZERO: dark scene, exposure or transport must be distinguished on board')
    if histogram[1023] / size > .05:
        warnings.append('SATURATION: inspect exposure/lighting; no automatic sensor writes')
    return dict(min=occupied[0], max=occupied[-1], mean=round(mean, 4),
                standard_deviation=round(variance ** .5, 4), unique_values=len(occupied),
                zero_fraction=histogram[0] / size, saturation_fraction=histogram[1023] / size,
                histogram=histogram, parity_means=[round(s / (size // 4), 4) for s in parity],
                row_means=rows, warnings=warnings, units='unsigned RAW10 code 0..1023')


def render_raw(values, order, width, height, padding):
    """Independent integer reference for the fixed 1920x1080 C contract.

    All centre samples for these two output profiles are interior pixels, so
    precomputed 3x3 colour offsets have the same neighbour set as production C.
    Average before 10->8 conversion; no gamma/black-level/WB/model enhancement.
    """
    v.require(order in PATTERNS and (width, height, padding) in ((640, 640, 114), (128, 160, 0)),
              'Unsupported rendering profile')
    pattern = PATTERNS[order]
    image_height = 360 if width == 640 else 72
    pad_y = (height - image_height) // 2
    offsets = {}
    for py in range(2):
        for px in range(2):
            own = pattern[py * 2 + px]
            for c in range(3):
                offsets[py * 2 + px, c] = (0,) if c == own else tuple(
                    dy * 1920 + dx for dy in range(-1, 2) for dx in range(-1, 2)
                    if pattern[((py + dy) % 2) * 2 + (px + dx) % 2] == c)
    out = bytearray([padding]) * (width * height * 3)
    for y in range(image_height):
        sy = ((2 * y + 1) * 1080) // (2 * image_height)
        for x in range(width):
            sx = ((2 * x + 1) * 1920) // (2 * width)
            v.require(0 < sx < 1919 and 0 < sy < 1079, 'Reference needs an interior centre')
            at = sy * 1920 + sx
            parity = (sy % 2) * 2 + sx % 2
            for c in range(3):
                neighbours = offsets[parity, c]
                value = sum(values[at + delta] for delta in neighbours) // len(neighbours)
                out[((y + pad_y) * width + x) * 3 + c] = (value * 255 + 511) // 1023
    if width == 128:
        image = out[44 * 128 * 3:116 * 128 * 3]
        lo, hi = min(image), max(image)
        if hi > lo + 3:
            out[44 * 128 * 3:116 * 128 * 3] = bytes((x - lo) * 255 // (hi - lo) for x in image)
    return bytes(out)


def png(width, height, rgb):
    v.require(len(rgb) == width * height * 3 and width <= 640 and height <= 640, 'PNG input size')
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    scan = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(scan, 6)) + chunk(b'IEND', b''))


def load_file(path, maximum):
    v.require(path.is_file() and not path.is_symlink() and 0 < path.stat().st_size <= maximum,
              'Missing/nonregular/oversized input: ' + path.name)
    data = path.read_bytes()
    v.require(len(data) <= maximum, 'Input grew while reading')
    return data


def unique_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            v.require(key not in result, 'Duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(v.Rejected('Nonfinite JSON')))


def inference(log_dir, metadata):
    v.require(log_dir.is_dir() and not log_dir.is_symlink(), 'Invalid inference evidence directory')
    summary_bytes = load_file(log_dir / 'summary.json', 65536)
    summary = unique_json(summary_bytes)
    v.require(isinstance(summary, dict), 'Expected report JSON object')
    p_bytes = load_file(log_dir / 'producer.log', 1024 * 1024)
    d_bytes = load_file(log_dir / 'viewer.log', 512 * 1024)
    for name, data in (('producer.log', p_bytes), ('viewer.log', d_bytes)):
        v.require(sha(data) == summary.get(name + '_sha256'), 'Evidence log SHA mismatch: ' + name)
    v.require(summary.get('result') == 'SINGLE_FRAME_PROTOCOL_ACCEPTED_NOT_VISUAL_ACCEPTANCE'
              and summary.get('stage') == 'complete' and summary.get('version') == 201,
              'Incomplete/incompatible v201 report')
    v.require(summary.get('metadata') == metadata, 'Inference report belongs to a different bundle')
    p, d = p_bytes.decode('utf-8'), d_bytes.decode('utf-8')
    target = v.exactly(r'^COMMAND camera capture (\S+)$', p, 'Capture command')
    v.directory(target)
    v.validate_capture(p, target)
    v.require(v.manifest(p) == metadata, 'Console COMPLETE differs from bundle')
    command = v.exactly(r'^COMMAND npu yolo-ui (\S+)$', p, 'NPU command')
    v.require(command == target + '/model-rgb.bin', 'Wrong input command path')
    ready = v.exactly(r'display: model background input-crc32=([0-9a-fA-F]{8}) '
                      r'origin=file; waiting for matching inference', d, 'UI readiness')
    v.require(int(ready, 16) == metadata['crc32'], 'UI input CRC differs')
    result = v.producer_result(p, metadata['crc32'])
    v.consumer_result(d, result)
    v.require(summary.get('inference') == json.loads(json.dumps(result)), 'Summary and parsed detections differ')
    return result, {name: sha(data) for name, data in
                    (('summary.json', summary_bytes), ('producer.log', p_bytes), ('viewer.log', d_bytes))}


def centi(box):
    return tuple(int(x.split('.')[0]) * 100 + int(x.split('.')[1]) for x in box.split(','))


def source_box(box):
    x1, y1, x2, y2 = box
    x1, x2 = max(0, min(64000, x1)), max(0, min(64000, x2))
    y1, y2 = max(14000, min(50000, y1)), max(14000, min(50000, y2))
    if x1 >= x2 or y1 >= y2:
        return None
    return (x1 * 3, (y1 - 14000) * 3, x2 * 3, (y2 - 14000) * 3)


def rectangle(rgb, width, height, box, color):
    x1, y1, x2, y2 = box
    for y in range(max(0, y1), min(height, y2 + 1)):
        for x in range(max(0, x1), min(width, x2 + 1)):
            if x <= x1 + 1 or x >= x2 - 1 or y <= y1 + 1 or y >= y2 - 1:
                rgb[(y * width + x) * 3:(y * width + x) * 3 + 3] = bytes(color)


def visualizations(model, preview, result):
    overlay, original = bytearray(model), bytearray(preview)
    lcd = bytearray((16, 24, 32)) * (128 * 160)
    for y in range(128):
        for x in range(128):
            at = (((5 * y + 2) * 640) + 5 * x + 2) * 3
            r, g, b = model[at:at + 3]
            # RGB565 unpacked for host preview; no LVGL antialias/panel claim.
            color = ((r >> 3) * 255 // 31, (g >> 2) * 255 // 63, (b >> 3) * 255 // 31)
            lcd[((y + 24) * 128 + x) * 3:((y + 24) * 128 + x) * 3 + 3] = bytes(color)
    band = (37, 166, 90) if result and result['boxes'] else (68, 80, 92)
    lcd[:128 * 18 * 3] = bytes(band) * (128 * 18)
    rows = []
    for row in result['detections'] if result else []:
        index, cls, whole, part, encoded = row
        box = centi(encoded)
        source = source_box(box)
        color = COLORS[int(cls) % 4]
        rectangle(overlay, 640, 640, tuple(p // 100 for p in box), color)
        if source:
            rectangle(original, 128, 160, (source[0] * 128 // 192000,
                      44 + source[1] * 72 // 108000, source[2] * 128 // 192000,
                      44 + source[3] * 72 // 108000), color)
        shown = int(index) < result['boxes']
        if shown:
            rounded = tuple(min(640, (p + 50) // 100) for p in box)
            x1, y1, x2, y2 = (min(127, rounded[0] // 5), min(151, 24 + rounded[1] // 5),
                               min(127, rounded[2] // 5), min(151, 24 + rounded[3] // 5))
            if x2 <= x1:
                x2 = min(127, x1 + 1)
            if y2 <= y1:
                y2 = min(151, y1 + 1)
            rectangle(lcd, 128, 160, (x1, y1, x2, y2), color)
        rows.append(dict(index=int(index), class_id=int(cls), score=f'{whole}.{part}',
                         model_box_centi=box, source_box_centi=source, shown_on_lcd=shown,
                         source_mapping='intersects-source' if source else 'padding-only'))
    return {'model.png': png(640, 640, model), 'model-boxes.png': png(640, 640, overlay),
            'preview-boxes.png': png(128, 160, original), 'lcd-schematic.png': png(128, 160, lcd)}, rows


def document(report, images, label):
    esc = lambda value: html.escape(str(value), quote=True)
    panels = [('model.png', '模型原输入：线性RGB，114填充，不增强'),
              ('model-boxes.png', '模型空间：全部检测框'),
              ('preview-boxes.png', '预览空间：显示用拉伸＋原图映射框'),
              ('lcd-schematic.png', '128×160坐标/RGB565示意，非屏幕截图')]
    figures = ''.join('<figure><img alt="' + esc(title) + '" src="data:image/png;base64,' +
                      base64.b64encode(images[name]).decode('ascii') + '"><figcaption>' + esc(title) +
                      '</figcaption></figure>' for name, title in panels)
    rows = ''.join('<tr>' + ''.join('<td>' + esc(value) + '</td>' for value in
                   (r['index'], r['class_id'], r['score'], r['model_box_centi'],
                    r['source_box_centi'], r['source_mapping'], r['shown_on_lcd'])) + '</tr>'
                   for r in report['detections'])
    stats = report['raw']
    metrics = esc(f"RAW范围 {stats['min']}..{stats['max']} / 1023；均值 {stats['mean']}；"
                  f"标准差 {stats['standard_deviation']}；零值 {stats['zero_fraction']:.2%}；"
                  f"饱和 {stats['saturation_fraction']:.2%}；Bayer {report['bayer']}")
    warnings = ''.join('<li>' + esc(w) + '</li>' for w in stats['warnings']) or '<li>未触发简单数值告警；不代表画质通过。</li>'
    notice = ('主机合成示例，仅测试报告外观。没有摄像头采集或NPU推理，不能作为板测证据。'
              if report.get('synthetic_demo') else
              '仅证明下载文件与CPU预处理算法一致；不是摄像头实拍来源、色彩、检测准确率、'
              '面板显示或实时FPS通过。所有图均为离线派生图，不是板上截图。不自动调传感器/白平衡/曝光。')
    footer = ('本示例只有DEMO.json，不生成实际验收用REPORT.json。'
              if report.get('synthetic_demo') else
              '同目录 REPORT.json 保存全部输入/输出SHA、直方图及逐行统计。原始文件保持不变。')
    return ('<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<meta http-equiv="Content-Security-Policy" content="default-src \'none\'; '
            'img-src data:; style-src \'unsafe-inline\'; base-uri \'none\'; form-action \'none\'">'
            '<title>A733 单帧离线验收</title><style>'
            'body{background:#101820;color:#e4edf5;font:15px/1.7 system-ui;margin:28px auto;padding:0 20px;max-width:1300px}'
            'h1{font-size:28px}.note{border-left:4px solid #ffcc00;padding:12px;background:#202e3c}'
            '.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}'
            'figure{margin:0;padding:14px;background:#202e3c;border-radius:8px}img{width:100%;height:260px;object-fit:contain;image-rendering:pixelated}'
            'figcaption{margin-top:10px}table{border-collapse:collapse;min-width:900px}td,th{border:1px solid #456;padding:8px;text-align:left}'
            '.scroll{overflow:auto}code{overflow-wrap:anywhere}a{color:#00bfff}</style></head><body>'
            '<h1>A733 / 13M214 单帧离线验收</h1><p>' + esc(label) + '</p>'
            '<p class="note">' + esc(notice) + '</p>'
            '<p>模型输入 NuttX CRC：<code>' + esc(report['input_crc32']) + '</code>；推理日志绑定：' +
            esc(report['inference_bound']) + '</p><p>' + metrics + '</p><ul>' + warnings + '</ul>'
            '<div class="grid">' + figures + '</div><h2>坐标映射</h2>'
            '<p>模型640×640，原图1920×1080缩放成640×360，上下各填140行；先裁掉填充再映射原图。'
            '表中坐标单位为百分之一像素。仅落在填充区的框不伪造原图坐标。LCD最多显示前七框。</p>'
            '<div class="scroll"><table><thead><tr><th>序号</th><th>类别ID</th><th>分数</th><th>模型坐标</th>'
            '<th>原图坐标</th><th>映射</th><th>LCD显示</th></tr></thead><tbody>' + rows +
            '</tbody></table></div><p>没有推理日志时仅展示图像诊断，绝不生成虚构检测框。'
            + esc(footer) + '</p></body></html>')


def audit(bundle, output, log_dir=None):
    bundle, output = Path(bundle).resolve(), Path(output).absolute()
    v.require(not output.exists() and not output.is_symlink(), 'Output must be NEW')
    v.require(not output.resolve().is_relative_to(bundle) and not bundle.is_relative_to(output.resolve()),
              'Report must not overlap original bundle')
    if log_dir:
        log_dir = Path(log_dir).resolve()
        v.require(not output.resolve().is_relative_to(log_dir) and not log_dir.is_relative_to(output.resolve()),
                  'Report must not overlap inference evidence')
    check = v.offline_bundle(bundle)
    data = {name: load_file(bundle / name, entry['bytes']) for name, entry in check['files'].items()}
    v.require(all(sha(data[name]) == entry['sha256'] for name, entry in check['files'].items()),
              'Bundle changed during snapshot')
    metadata = v.manifest(data['COMPLETE.txt'].decode('ascii'))
    values = pixels(data['frame.raw'])
    order = metadata['fields']['pixel'].split('-')[1]
    expected = render_raw(values, order, 640, 640, 114)
    v.require(expected == data['model-rgb.bin'], 'RAW -> model RGB mismatch (layout/Bayer/conversion/stale file)')
    preview = render_raw(values, order, 128, 160, 0)
    v.require(preview == data['preview.rgb'], 'RAW -> display preview mismatch')
    result, log_hashes = inference(log_dir, metadata) if log_dir else (None, {})
    images, rows = visualizations(expected, preview, result)
    report = dict(version=202, result='OFFLINE_PREPROCESSING_VERIFIED_NOT_HARDWARE',
                  input_crc32=check['input_crc32'], bayer=order, raw=raw_stats(values),
                  files=check['files'], inference_bound=result is not None, log_sha256=log_hashes,
                  detections=rows, cpu_preprocessing_exact=True, hardware_verified=False,
                  reference_cpu_source_sha256=CPU_SOURCE_SHA, reference_export_source_sha256=EXPORT_SOURCE_SHA,
                  accuracy_verified=False, colour_qualified=False, fps_measured=False,
                  output_sha256={name: sha(payload) for name, payload in images.items()})
    page = document(report, images, bundle.name).encode('utf-8')
    report['output_sha256']['report.html'] = sha(page)
    # Publish only after all input checks. An I/O failure leaves partial files,
    # never a committed REPORT.json. Existing input/output files are untouched.
    output.mkdir(parents=True, exist_ok=False)
    for name, payload in {**images, 'report.html': page}.items():
        with (output / name).open('xb') as stream:
            stream.write(payload)
    with (output / 'REPORT.partial').open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(report, stream, indent=2)
    (output / 'REPORT.partial').rename(output / 'REPORT.json')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--inference-dir', type=Path)
    args = parser.parse_args()
    result = audit(args.bundle, args.output, args.inference_dir)
    print(json.dumps({k: result[k] for k in ('result', 'input_crc32', 'inference_bound', 'hardware_verified')}, indent=2))
    print('Report:', args.output / 'report.html')


if __name__ == '__main__':
    try:
        main()
    except (v.Rejected, OSError, ValueError, KeyError, TypeError) as error:
        print('STOP:', error, file=sys.stderr)
        sys.exit(2)
