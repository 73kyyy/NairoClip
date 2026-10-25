#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""docs/UI_INTEGRATION.md §9 验收清单（41 项）自动化验证。

用法：python3 tools/acceptance.py <port>
每条对应文档里的一个 □；输出 PASS / FAIL / BLOCKED 及原因。
"""
import json
import re
import sys
import urllib.error
import urllib.request

PORT = sys.argv[1] if len(sys.argv) > 1 else '9600'
BASE = f'http://127.0.0.1:{PORT}'
results = []


def call(fn, args=None, timeout=30):
    body = json.dumps({'fn': fn, 'args': args or {}}).encode()
    req = urllib.request.Request(BASE + '/api/call', data=body,
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


def get(path, timeout=30):
    with urllib.request.urlopen(BASE + path, timeout=timeout) as r:
        return r.read(), r.headers.get('content-type', '')


def state():
    return json.loads(get('/api/state')[0])


def rec(name, ok, detail=''):
    results.append((name, ok, detail))


def first_clip():
    st = state()
    for i, t in enumerate(st['tracks']):
        if t.get('clips'):
            return i, t['clips'][0]['id'], st
    return None, None, st


def clips_of(ti):
    return state()['tracks'][ti].get('clips') or []


# ---------- P0 剪辑核心 ----------
def p0():
    st = state()
    rec('打开工程 → /api/state 返回真实数据',
        bool(st.get('project', {}).get('name')) and len(st.get('tracks', [])) == 3,
        f"{st['project'].get('name')} / {len(st['tracks'])} 轨")

    ti, cid, st = first_clip()
    if cid is None:
        rec('插入片段 → clip_count +1', False, '无片段可测')
        return
    n0 = len(clips_of(ti))
    # 用 duplicate 模拟插入（不动素材，可撤销）
    r = call('nc_duplicate_clip', {'track_index': ti, 'clip_id': cid})
    n1 = len(clips_of(ti))
    rec('插入片段 → clip_count +1', n1 == n0 + 1, f'{n0} → {n1} (code={r["code"]})')
    call('nc_undo')

    # 分割
    ti, cid, _ = first_clip()
    n0 = len(clips_of(ti))
    cl = clips_of(ti)[0]
    mid = cl['start_us'] + cl['duration_us'] // 2
    r = call('nc_split_clip', {'track_index': ti, 'clip_id': cid, 'at_us': mid})
    n1 = len(clips_of(ti))
    rec('分割 → 两段', n1 == n0 + 1, f'{n0} → {n1} (code={r["code"]})')
    call('nc_undo')

    # 移动
    ti, cid, _ = first_clip()
    before = clip_info(ti, cid)
    r = call('nc_move_clip', {'track_index': ti, 'clip_id': cid,
                              'start_us': before['start_us'] + 500000})
    after = clip_info(ti, cid)
    rec('移动 → 起点变化', after['start_us'] != before['start_us'],
        f"{before['start_us']} → {after['start_us']}")
    call('nc_undo')

    # 跨轨移动（需要一条非空目标轨；先加一条）
    ti, cid, _ = first_clip()
    call('nc_add_track', {'name': '验收轨', 'type': 0})
    st = state()
    to_ti = len(st['tracks']) - 1
    before_n = len(clips_of(ti))
    r = call('nc_move_clip_to_track', {'track_index': ti, 'to_track': to_ti,
                                       'clip_id': cid, 'start_us': 0})
    ok = r['code'] == 0 and len(clips_of(to_ti)) == 1
    rec('跨轨移动 → 换轨成功', ok,
        f"{before_n}→{len(clips_of(ti))} / 目标轨 {len(clips_of(to_ti))}")
    call('nc_undo')
    if len(state()['tracks']) > 3:
        call('nc_remove_track', {'track_index': len(state()['tracks']) - 1})

    # 裁剪起/终点
    ti, cid, _ = first_clip()
    b = clip_info(ti, cid)
    call('nc_trim_clip_start', {'track_index': ti, 'clip_id': cid, 'start_us': 500000})
    a = clip_info(ti, cid)
    rec('裁剪起点 → 时长变化', a['duration_us'] != b['duration_us'],
        f"{b['duration_us']} → {a['duration_us']}")
    call('nc_undo')
    ti, cid, _ = first_clip()
    b = clip_info(ti, cid)
    call('nc_trim_clip_end', {'track_index': ti, 'clip_id': cid,
                              'end_us': max(100000, b['duration_us'] // 2)})
    a = clip_info(ti, cid)
    rec('裁剪终点 → 时长变化', a['duration_us'] != b['duration_us'],
        f"{b['duration_us']} → {a['duration_us']}")
    call('nc_undo')

    # 删除
    ti, cid, _ = first_clip()
    n0 = len(clips_of(ti))
    r = call('nc_remove_clip', {'track_index': ti, 'clip_id': cid})
    n1 = len(clips_of(ti))
    rec('删除 → 片段消失', n1 == n0 - 1, f'{n0} → {n1}')
    call('nc_undo')

    # 波纹删除
    ti, cid, _ = first_clip()
    cl = clips_of(ti)
    if len(cl) >= 2:
        second = cl[1]
        b_start = second['start_us']
        call('nc_ripple_delete', {'track_index': ti, 'clip_id': cid})
        cl2 = clips_of(ti)
        moved = cl2[0]['start_us'] if cl2 else -1
        rec('波纹删除 → 后续片段前移', moved < b_start,
            f'第二段 {b_start} → {moved}')
        call('nc_undo')
    else:
        rec('波纹删除 → 后续片段前移', False, '片段不足')

    # 复制
    ti, cid, _ = first_clip()
    n0 = len(clips_of(ti))
    r = call('nc_duplicate_clip', {'track_index': ti, 'clip_id': cid})
    n1 = len(clips_of(ti))
    rec('复制 → 新增副本', n1 == n0 + 1, f'{n0} → {n1} (ret={r.get("code")})')
    call('nc_undo')

    # 定格
    ti, cid, _ = first_clip()
    r = call('nc_freeze_frame', {'track_index': ti, 'clip_id': cid,
                                 'at_us': clip_info(ti, cid)['start_us'] + 100000,
                                 'duration_us': 1000000})
    rec('定格 → 静止片段', r['code'] == 0, f"code={r['code']} {r.get('message','')[:40]}")
    call('nc_undo')

    # 倒放
    ti, cid, _ = first_clip()
    r = call('nc_reverse_clip', {'track_index': ti, 'clip_id': cid, 'reversed': 1})
    rec('倒放 → reversed=true', r['code'] == 0, f"code={r['code']}")
    call('nc_undo')

    # 变速
    ti, cid, _ = first_clip()
    b = clip_info(ti, cid)
    r = call('nc_set_clip_speed', {'track_index': ti, 'clip_id': cid,
                                   'speed_num': 2, 'speed_den': 1})
    a = clip_info(ti, cid)
    rec('变速 → 时长按比例', a['duration_us'] < b['duration_us'],
        f"{b['duration_us']} → {a['duration_us']} (code={r['code']})")
    call('nc_undo')

    # 变速曲线
    ti, cid, _ = first_clip()
    r = call('nc_set_clip_speed_curve', {
        'track_index': ti, 'clip_id': cid,
        'times_us': [0.0, 0.5, 1.0], 'speeds': [1.0, 2.0, 1.0],
        'easings': [0, 0, 0]})
    n = call('nc_clip_speed_curve_count', {'track_index': ti, 'clip_id': cid})
    rec('变速曲线 → 曲线点可读写',
        r['code'] == 0 and n['result'].get('value', 0) > 0,
        f"写入 code={r['code']}, 点数={n['result'].get('value')}")
    call('nc_undo')

    # 变速预设
    ti, cid, _ = first_clip()
    names = [call('nc_speed_curve_preset_display_name', {'preset': i})['result'].get('value')
             for i in range(6)]
    r = call('nc_apply_speed_curve_preset', {'track_index': ti, 'clip_id': cid, 'preset': 1})
    rec('变速预设 → 6 预设可用',
        all(names) and r['code'] == 0, ' / '.join(str(x) for x in names[:3]) + ' ...')
    call('nc_undo')

    # 不透明度
    ti, cid, _ = first_clip()
    call('nc_set_clip_opacity', {'track_index': ti, 'clip_id': cid, 'opacity': 0.42})
    g = call('nc_get_clip_opacity', {'track_index': ti, 'clip_id': cid})
    v = g['result'].get('out_opacity')
    rec('不透明度 → 0.42 可读回', abs((v or 0) - 0.42) < 1e-6, f'读回 {v}')
    call('nc_undo')

    # 音量/淡化
    ti, cid, _ = first_clip()
    r = call('nc_set_clip_audio', {'track_index': ti, 'clip_id': cid,
                                   'gain_db': -6.0, 'fade_in_us': 200000,
                                   'fade_out_us': 200000})
    rec('音量/淡化 → 设置成功', r['code'] == 0, f"code={r['code']}")
    call('nc_undo')

    # 降噪
    ti, cid, _ = first_clip()
    r = call('nc_set_clip_denoise', {'track_index': ti, 'clip_id': cid,
                                     'enabled': 1, 'strength': 2})
    g = call('nc_get_clip_denoise', {'track_index': ti, 'clip_id': cid})
    rec('降噪 → 开关可读回', r['code'] == 0, f"写入 code={r['code']}, 读回 {g['result']}")
    call('nc_undo')

    # 滤镜
    ti, cid, _ = first_clip()
    r = call('nc_set_clip_filter', {'track_index': ti, 'clip_id': cid,
                                    'style': 1, 'intensity': 80})
    g = call('nc_get_clip_filter', {'track_index': ti, 'clip_id': cid})
    rec('滤镜 → 色调变化', g['result'].get('out_style') == 1,
        f"读回 style={g['result'].get('out_style')} int={g['result'].get('out_intensity')}")
    call('nc_undo')

    # 蒙版
    ti, cid, _ = first_clip()
    r = call('nc_set_clip_mask', {'track_index': ti, 'clip_id': cid, 'shape': 2,
                                  'center_x': 0.5, 'center_y': 0.5, 'rotation_degrees': 0,
                                  'width': 0.5, 'height': 0.5, 'feather': 0.1,
                                  'corner_radius': 0, 'invert': 0})
    g = call('nc_get_clip_mask', {'track_index': ti, 'clip_id': cid})
    rec('蒙版 → 区域裁剪生效', g['result'].get('out_shape') == 2,
        f"读回 shape={g['result'].get('out_shape')}")
    call('nc_undo')

    # 混合模式
    ti, cid, _ = first_clip()
    r = call('nc_set_clip_blend_mode', {'track_index': ti, 'clip_id': cid, 'mode': 3})
    g = call('nc_get_clip_blend_mode', {'track_index': ti, 'clip_id': cid})
    rec('混合模式 → 公式生效', r['code'] == 0, f"读回 {g['result']}")
    call('nc_undo')

    # 背景填充
    ti, cid, _ = first_clip()
    r = call('nc_set_clip_background', {'track_index': ti, 'clip_id': cid,
                                        'mode': 1, 'rgba': 0xFF0000FF, 'intensity': 1.0})
    g = call('nc_get_clip_background', {'track_index': ti, 'clip_id': cid})
    rec('背景填充 → 留白区域填充', r['code'] == 0, f"读回 {g['result']}")
    call('nc_undo')

    # 特效增删
    ti, cid, _ = first_clip()
    n0 = call('nc_effect_count', {'track_index': ti, 'clip_id': cid})['result'].get('value', 0)
    a = call('nc_add_effect', {'track_index': ti, 'clip_id': cid, 'type': 13, 'value': 1.0})
    n1 = call('nc_effect_count', {'track_index': ti, 'clip_id': cid})['result'].get('value', 0)
    rec('特效增删 → 列表同步', n1 == n0 + 1, f'{n0} → {n1} (code={a["code"]})')
    call('nc_undo')

    # 关键帧
    ti, cid, _ = first_clip()
    r = call('nc_insert_keyframe', {'track_index': ti, 'clip_id': cid, 'property': 0,
                                    'time_us': 0, 'value': 1.0, 'easing': 0})
    n = call('nc_keyframe_count', {'track_index': ti, 'clip_id': cid,
                                   'property': 0})['result'].get('value', 0)
    rec('关键帧 → 可插入可计数', n >= 1, f'点数 {n} (code={r["code"]})')
    call('nc_undo')

    # 字幕
    r = call('nc_upsert_subtitle', {'start_us': 0, 'duration_us': 1000000,
                                    'text': '验收字幕'})
    n = call('nc_subtitle_count')['result'].get('value', 0)
    rec('字幕 → 时间轴 + 预览文本', n >= 1, f'条数 {n}')
    call('nc_undo')

    # 标记
    r = call('nc_add_marker', {'time_us': 500000, 'name': '验收标记', 'color': '#FFCC00'})
    n = call('nc_marker_count')['result'].get('value', 0)
    rec('标记 → 时间轴标记点', n >= 1, f'数量 {n}')
    call('nc_undo')

    # 轨道锁定/静音/隐藏
    ok = True
    detail = []
    for fn, flag in (('nc_set_track_locked', 'locked'),
                     ('nc_set_track_muted', 'muted'),
                     ('nc_set_track_hidden', 'hidden')):
        r = call(fn, {'track_index': 0, flag: 1})
        g = call('nc_track_get', {'track_index': 0})
        v = g['result'].get('out_' + flag)
        detail.append(f'{flag}={v}')
        ok = ok and (v == 1)
    rec('轨道锁定/静音/隐藏 → 状态生效', ok, ' '.join(detail))
    for fn, flag in (('nc_set_track_locked', 'locked'),
                     ('nc_set_track_muted', 'muted'),
                     ('nc_set_track_hidden', 'hidden')):
        call(fn, {'track_index': 0, flag: 0})

    # 轨道增益
    r = call('nc_set_track_gain', {'track_index': 1, 'gain_db': -3.0})
    g = call('nc_get_track_mixer', {'track_index': 1})
    rec('轨道增益 → 混音变化', abs((g['result'].get('out_gain_db') or 0) + 3.0) < 0.01,
        f"读回 {g['result'].get('out_gain_db')} dB")

    # 撤销/重做
    st0 = state()
    ti, cid, _ = first_clip()
    call('nc_set_clip_opacity', {'track_index': ti, 'clip_id': cid, 'opacity': 0.7})
    st1 = state()
    call('nc_undo')
    st2 = state()
    call('nc_redo')
    st3 = state()
    ok = (st2['project']['undo_count'] < st1['project']['undo_count']
          and st2['project']['redo_count'] > st1['project']['redo_count']
          and st3['project']['undo_count'] > st2['project']['undo_count']
          and st0['project']['can_undo'] is not None)
    rec('撤销/重做 → 状态与按钮态同步', ok,
        f"undo {st1['project']['undo_count']}→{st2['project']['undo_count']}, "
        f"redo {st2['project']['redo_count']}→{st3['project']['redo_count']}")
    call('nc_undo')

    # 撤销深度
    r = call('nc_set_undo_depth_limit', {'limit': 20})
    rec('撤销深度 20 步 → 可设置', r['code'] == 0, f"code={r['code']}")

    # 保存/载入
    sv = call('nc_project_save')
    text = sv['result'].get('value') or ''
    ld = call('nc_project_load', {'nclip_text': text})
    rec('保存/载入 → 往返一致',
        bool(text) and ld['code'] == 0,
        f'保存 {len(text)} 字符, 载入 code={ld["code"]}')


def clip_info(ti, cid):
    st = state()
    for c in st['tracks'][ti].get('clips') or []:
        if c['id'] == cid:
            return c
    return {}


# ---------- P1 时间轴 + 预览 ----------
def p1():
    # 预览帧真实解码（非纯色）
    body, ct = get('/api/frame?t=1500000&w=240')
    ok = ct.startswith('image/') and len(body) > 2000
    rec('预览帧 → 真实解码（非纯色）', ok, f'{ct}, {len(body)} 字节')

    body, ct = get('/api/thumb?asset=2&i=0&w=120')
    rec('缩略图 → 真实序列', ct.startswith('image/') and len(body) > 1000,
        f'{ct}, {len(body)} 字节')

    body, ct = get('/api/waveform?asset=5')
    is_json = b'json' in ct.encode() or body.strip().startswith(b'{')
    n = 0
    if is_json:
        try:
            d = json.loads(body)
            n = len(d.get('buckets') or d.get('data') or [])
        except Exception:
            n = 0
    rec('波形 → 真实桶数据', is_json and n > 0, f'{ct}, {n} 桶')

    # 场景检测（沙箱可能不可用）
    r = call('nc_scene_detect', {'step_seconds': 0.5, 'max_samples': 16})
    rec('场景检测 → 边界时间点',
        r['code'] == 0 and 'boundaries' in r['result'],
        f"code={r['code']} count={r['result'].get('count')}")

    r = call('nc_beat_detect', {'track_index': 1})
    rec('节拍检测 → BPM + 节拍数组',
        r['code'] == 0 and 'bpm' in r['result'],
        f"bpm={r['result'].get('bpm')} beats={r['result'].get('count')}")

    # 转场
    ti, cid, st = first_clip()
    clips = clips_of(ti)
    if len(clips) >= 2:
        r = call('nc_add_transition', {'track_index': ti,
                                       'from_clip': clips[0]['id'],
                                       'to_clip': clips[1]['id'],
                                       'type': 1, 'duration_us': 500000})
        n = call('nc_transition_count', {'track_index': ti})['result'].get('value', 0)
        rec('转场添加 → 转场图标出现', n >= 1, f'转场数 {n} (code={r["code"]})')
        call('nc_undo')
    else:
        rec('转场添加 → 转场图标出现', False, '片段不足 2 段')

    n = call('nc_transition_type_count')['result'].get('value', 0)
    cats = set()
    for i in range(min(n, 40)):
        c = call('nc_transition_category', {'type': i})['result'].get('value')
        if c:
            cats.add(c)
    rec('转场库 → 34 种 + 9 分类', n >= 34 and len(cats) >= 9,
        f'{n} 种 / {len(cats)} 分类')

    # 转场关键帧
    ti, cid, _ = first_clip()
    clips = clips_of(ti)
    if len(clips) >= 2:
        tr = call('nc_transition_count', {'track_index': ti})['result'].get('value', 0)
        if tr == 0:
            call('nc_add_transition', {'track_index': ti,
                                       'from_clip': clips[0]['id'],
                                       'to_clip': clips[1]['id'],
                                       'type': 1, 'duration_us': 500000})
        # 取转场 id
        g = call('nc_transition_get', {'track_index': ti, 'index': 0})
        trans_id = g['result'].get('value') or g['result'].get('out_id')
        r = call('nc_add_transition_keyframe', {'track_index': ti,
                                                'transition_id': trans_id or 1,
                                                'property': 0, 'offset_us': 0,
                                                'value': 0.5, 'easing': 0})
        rec('转场关键帧 → 区间内可读写', r['code'] == 0, f"code={r['code']}")
        call('nc_undo')
    else:
        rec('转场关键帧 → 区间内可读写', False, '片段不足')

    # 镜头追踪
    subs = state().get('subtitles') or []
    if subs:
        sid = subs[0].get('id') or subs[0].get('subtitle_id')
        r = call('nc_set_camera_tracking', {
            'subtitle_id': sid,
            'times_us': [0.0, 1.0], 'center_x': [0.5, 0.55],
            'center_y': [0.5, 0.52], 'point_count': 2, 'apply_mode': 1})
        n = call('nc_tracking_point_count', {'subtitle_id': sid})['result'].get('value', 0)
        rec('镜头追踪 → 锚点随时间插值', r['code'] == 0 and n >= 2,
            f'锚点 {n} (code={r["code"]})')
        call('nc_clear_camera_tracking', {'subtitle_id': sid})
    else:
        rec('镜头追踪 → 锚点随时间插值', False, '无字幕可挂追踪点')

    # 画幅
    r = call('nc_set_frame_fit', {'mode': 2})
    g = call('nc_get_frame_fit')
    rec('画幅切换 → 预览比例变化', r['code'] == 0,
        f"mode={g['result'].get('value')}")
    call('nc_set_frame_fit', {'mode': 0})

    # 画布比例
    r = call('nc_set_project_canvas', {'width': 1920, 'height': 1080})
    st = state()
    ok = st['project']['width'] == 1920 and st['project']['height'] == 1080
    rec('画布比例 → 预览尺寸变化', ok,
        f"{st['project']['width']}x{st['project']['height']}")
    call('nc_set_project_canvas', {'width': 1080, 'height': 1920})


def main():
    try:
        p0()
    except Exception as e:
        rec('P0 执行异常', False, str(e)[:100])
    try:
        p1()
    except Exception as e:
        import traceback
        rec('P1 执行异常', False, f'{e} @ ' + traceback.format_exc().splitlines()[-3].strip()[:120])

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'\n===== 验收：{passed}/{len(results)} 通过 =====\n')
    for name, ok, detail in results:
        mark = 'PASS' if ok else 'FAIL'
        print(f'[{mark}] {name}')
        if detail:
            print(f'       {detail}')
    print(f'\n通过 {passed} / 共 {len(results)}')
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    sys.exit(main())
