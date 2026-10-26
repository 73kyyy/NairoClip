/* =============================================================================
   NairoClip Mobile · 三级面板规格表（纯数据，不含渲染逻辑）
   -----------------------------------------------------------------------------
   与 editor.js 分开，避免 60+ 面板把主逻辑撑乱。

   结构约定：
     L3_SPEC[key] = {
       title   面板标题
       mode    'single'  单页：功能名加粗 + 竖排控件
               'dual'    双页：常规 / 区间（曲线）
               'grid'    宫格：先选预设，再进参数
       groups  [[控件key, ...], ...]  按页分组（dual 时两组 = 两页）
       grid    宫格预设数组（mode='grid'）
       note    面板底部灰字说明（未接引擎时如实标注）
     }

   每个控件的范围/默认值都取自 core 的权威定义：
     ClipProperty  Opacity[0,1] Scale[0.01,100] X/Y[-10,10] Rotation[-3600,3600] Volume[0,4]
     FilterStyle   14 种（None + 13 风格）
     MaskShape     1 线性 / 2 镜面 / 3 圆形 / 4 矩形 / 5 星形 / 6 爱心
     EffectType    13..40（画面/氛围/动感/人物）
     SpeedCurvePreset  0 自定义 1 蒙太奇 2 英雄时刻 3 子弹时间 4 跳跃 5 闪进 6 闪出
   ============================================================================= */
(function () {
  'use strict';

  /* ---------------- 控件定义 ---------------- */
  /* kind: slider 滑轨 / switch 开关 / seg 分段小控件 / color 色板 / number 数值框 / button 动作 */
  const C = {
    /* —— 可动画通道（ClipProperty 0..5）—— */
    opacity:   { kind: 'slider', name: '不透明度', icon: 'i-opacity', min: 0, max: 1, step: 0.01, def: 1,    unit: '%',  scale: 100, prop: 0 },
    scale:     { kind: 'slider', name: '缩放',     icon: 'i-expand',  min: 0.01, max: 100, step: 0.01, def: 1, unit: 'x',  prop: 1 },
    posx:      { kind: 'slider', name: 'X 位移',   icon: 'i-swap',    min: -10, max: 10, step: 0.01, def: 0,   unit: '',   prop: 2 },
    posy:      { kind: 'slider', name: 'Y 位移',   icon: 'i-swap',    min: -10, max: 10, step: 0.01, def: 0,   unit: '',   prop: 3 },
    rotation:  { kind: 'slider', name: '旋转',     icon: 'i-reset',   min: -180, max: 180, step: 1, def: 0,    unit: '°',  prop: 4 },
    volume:    { kind: 'slider', name: '音量',     icon: 'i-volume',  min: 0, max: 4, step: 0.01, def: 1,     unit: '%',  scale: 100, prop: 5 },

    /* —— 非动画标量 —— */
    speed:     { kind: 'slider', name: '速度',     icon: 'i-speed',   min: 0.1, max: 10, step: 0.01, def: 1,   unit: 'x' },
    filteramt: { kind: 'slider', name: '强度',     icon: 'i-filter',  min: 0, max: 100, step: 1, def: 60,    unit: '' },
    fxamt:     { kind: 'slider', name: '强度',     icon: 'i-fx',      min: 0, max: 100, step: 1, def: 50,    unit: '' },
    feather:   { kind: 'slider', name: '羽化',     icon: 'i-mask',    min: 0, max: 1, step: 0.01, def: 0.02,  unit: '%', scale: 100 },
    radius:    { kind: 'slider', name: '圆角',     icon: 'i-crop',    min: 0, max: 0.5, step: 0.01, def: 0,   unit: '%', scale: 100 },
    maskw:     { kind: 'slider', name: '宽度',     icon: 'i-expand',  min: 0.05, max: 2, step: 0.01, def: 1,  unit: 'x' },
    maskh:     { kind: 'slider', name: '高度',     icon: 'i-expand',  min: 0.05, max: 2, step: 0.01, def: 1,  unit: 'x' },
    maskrot:   { kind: 'slider', name: '旋转',     icon: 'i-reset',   min: -180, max: 180, step: 1, def: 0,   unit: '°' },
    fadein:    { kind: 'slider', name: '淡入',     icon: 'i-fade',    min: 0, max: 5, step: 0.1, def: 0,     unit: 's' },
    fadeout:   { kind: 'slider', name: '淡出',     icon: 'i-fade',    min: 0, max: 5, step: 0.1, def: 0,     unit: 's' },
    gain:      { kind: 'slider', name: '增益',     icon: 'i-volume',  min: -24, max: 12, step: 0.5, def: 0,   unit: 'dB' },
    threshold: { kind: 'slider', name: '阈值',     icon: 'i-adjust',  min: 0, max: 1, step: 0.01, def: 0.35,  unit: '' },
    spill:     { kind: 'slider', name: '溢色抑制', icon: 'i-palette', min: 0, max: 1, step: 0.01, def: 0.3,   unit: '' },
    chromakey: { kind: 'color',  name: '抠像色',   icon: 'i-palette', def: '#00FF00' },
    chromain:  { kind: 'slider', name: '内阈值',   icon: 'i-adjust', min: 0, max: 1, step: 0.01, def: 0.35, unit: '' },
    chromaout: { kind: 'slider', name: '外阈值',   icon: 'i-adjust', min: 0, max: 1, step: 0.01, def: 0.55, unit: '' },
    chromaq:   { kind: 'seg',    name: '精度',     icon: 'i-bolt', opts: ['快速', '标准', '精细'], def: 1 },
    chromaon:  { kind: 'switch', name: '启用抠像', icon: 'i-mask', def: 1 },
    blur:      { kind: 'slider', name: '模糊',     icon: 'i-bg',      min: 0, max: 100, step: 1, def: 40,    unit: '' },
    transdur:  { kind: 'slider', name: '时长',     icon: 'i-trans',   min: 0.2, max: 3, step: 0.1, def: 0.6,  unit: 's' },
    freezedur: { kind: 'slider', name: '定格时长', icon: 'i-freeze',  min: 0.5, max: 5, step: 0.1, def: 2,    unit: 's' },
    reverseon: { kind: 'switch', name: '开启倒放', icon: 'i-reverse', def: 0 },
    fontsize:  { kind: 'slider', name: '字号',     icon: 'i-text',    min: 12, max: 120, step: 1, def: 48,   unit: 'px' },
    stroke:    { kind: 'slider', name: '描边',     icon: 'i-text',    min: 0, max: 10, step: 0.5, def: 0,    unit: 'px' },
    ttsspeed:  { kind: 'slider', name: '语速',     icon: 'i-tts',     min: 0.5, max: 2, step: 0.05, def: 1,  unit: 'x' },
    pitch:     { kind: 'slider', name: '音调',     icon: 'i-note',    min: -12, max: 12, step: 1, def: 0,   unit: '' },
    offset:    { kind: 'slider', name: '时间偏移', icon: 'i-key',     min: -2, max: 2, step: 0.1, def: 0,    unit: 's' },
    sensitivity:{ kind: 'slider', name: '灵敏度',  icon: 'i-track',   min: 0, max: 100, step: 1, def: 50,   unit: '' },
    originality:{ kind: 'slider', name: '原创度',  icon: 'i-ai',      min: 0, max: 100, step: 1, def: 60,   unit: '%' },
    depthamt:  { kind: 'slider', name: '强度',     icon: 'i-layers',  min: 0, max: 100, step: 1, def: 50,   unit: '' },

    /* —— 开关 —— */
    keeppitch: { kind: 'switch', name: '保持音调不变', def: true },
    denoiseon: { kind: 'switch', name: '开启降噪',     def: false },
    invert:    { kind: 'switch', name: '反转蒙版',     def: false },
    overwrite: { kind: 'switch', name: '覆盖原字幕',   def: true },
    trackfollow:{ kind: 'switch', name: '跟随镜头',    def: true },

    /* —— 分段小控件 —— */
    denoiselv: { kind: 'seg', name: '强度', opts: ['关', '弱', '中', '强'], def: 0 },
    amplify:   { kind: 'seg', name: '输出', opts: ['人声', '伴奏', '两者'], def: 2 },
    valign:    { kind: 'seg', name: '对齐', opts: ['左', '中', '右'], def: 1 },
    depthmode: { kind: 'seg', name: '模式', opts: ['景深', '人像描边', '背景虚化'], def: 0 },
    trackmode: { kind: 'seg', name: '跟踪点', opts: ['自动', '手动'], def: 0 },
    quality:   { kind: 'seg', name: '精度', opts: ['快速', '标准', '精细'], def: 1 },

    /* —— 色板 / 数值 —— */
    textcolor: { kind: 'color', name: '颜色', def: '#FFFFFF',
                 opts: ['#FFFFFF', '#F0A63E', '#E5484D', '#2D8CFF', '#7ED957', '#16171C'] },
    bgcolor:   { kind: 'color', name: '背景色', def: '#000000',
                 opts: ['#000000', '#FFFFFF', '#232323', '#1B3A46', '#3A2A18', '#2A1B3A'] },
    canvasc:   { kind: 'color', name: '画布色', def: '#000000',
                 opts: ['#000000', '#FFFFFF', '#111413', '#202224'] },
    ratioW:    { kind: 'number', name: '宽', min: 16, max: 8192, def: 1080, unit: 'px' },
    ratioH:    { kind: 'number', name: '高', min: 16, max: 8192, def: 1920, unit: 'px' },

    /* —— 动作 —— */
    aiproc:    { kind: 'button', name: '开始处理', icon: 'i-ai' }
  };

  /* ---------------- 宫格预设 ---------------- */

  /* 滤镜：FilterStyle 1..13（14 为负片，一并列出） */
  const FILTERS = [
    { id: 1,  name: '电影', slug: 'movie' },
    { id: 2,  name: '复古', slug: 'retro' },
    { id: 3,  name: '清新', slug: 'fresh' },
    { id: 4,  name: '黑白', slug: 'black_white' },
    { id: 5,  name: '赛博', slug: 'cyber' },
    { id: 6,  name: '日系', slug: 'japanese' },
    { id: 7,  name: '胶片', slug: 'film' },
    { id: 8,  name: '暖阳', slug: 'warm_sun' },
    { id: 9,  name: '冷调', slug: 'cool_tone' },
    { id: 10, name: '港风', slug: 'hong_kong' },
    { id: 11, name: '高级灰', slug: 'high_gray' },
    { id: 12, name: '奶油', slug: 'cream' },
    { id: 13, name: '负片', slug: 'negative' }
  ];

  /* 特效：EffectType 13..40，按文档四大类 */
  const EFFECTS = {
    画面: [
      { id: 13, name: '故障' },   { id: 14, name: '分屏' },   { id: 15, name: '镜像' },
      { id: 16, name: '旋转模糊' }, { id: 17, name: '扫描线' }, { id: 18, name: 'RGB 分离' },
      { id: 29, name: '闪光' },   { id: 30, name: '马赛克' }
    ],
    氛围: [
      { id: 19, name: '雪花' }, { id: 20, name: '下雨' }, { id: 21, name: '光斑' },
      { id: 22, name: '老电影' }, { id: 23, name: '星光' }
    ],
    动感: [
      { id: 24, name: '缩放冲击' }, { id: 25, name: '甩镜' }, { id: 26, name: '变速闪烁' },
      { id: 27, name: '节奏律动' }, { id: 28, name: '运动模糊' }
    ],
    人物: [
      { id: 31, name: '抖动' }, { id: 32, name: '放大' }, { id: 33, name: '边缘检测' },
      { id: 34, name: '瘦身' }, { id: 35, name: '发光轮廓' }
    ]
  };

  /* 蒙版：MaskShape 1..6 */
  const MASKS = [
    { id: 1, name: '线性' }, { id: 2, name: '镜面' }, { id: 3, name: '圆形' },
    { id: 4, name: '矩形' }, { id: 5, name: '星形' }, { id: 6, name: '爱心' }
  ];

  /* 转场：仅列常用名，id 对应引擎 transitionTypes */
  /* 转场预设：id 必须等于 core::TransitionType 枚举值（0..33），
     直接作为 nc_add_transition 的 transition_type 传入。
     名称取自 core/src/transition.cpp 的 transitionName()，逐字一致。 */
  const TRANSITIONS = [
    { id: 0,  name: '交叉溶解',   cat: '基础' },
    { id: 1,  name: '淡入黑场',   cat: '基础' },
    { id: 2,  name: '从黑场淡入', cat: '基础' },
    { id: 3,  name: '闪白',       cat: '基础' },
    { id: 4,  name: '左向擦除',   cat: '几何' },
    { id: 5,  name: '右向擦除',   cat: '几何' },
    { id: 6,  name: '上滑',       cat: '运镜' },
    { id: 7,  name: '缩放',       cat: '运镜' },
    { id: 8,  name: '硬切',       cat: '基础' },
    { id: 9,  name: '色彩叠化',   cat: '叠化' },
    { id: 10, name: '模糊叠化',   cat: '叠化' },
    { id: 11, name: '拉远',       cat: '运镜' },
    { id: 12, name: '旋转',       cat: '运镜' },
    { id: 13, name: '甩镜',       cat: '运镜' },
    { id: 14, name: '缩放冲击',   cat: '运镜' },
    { id: 15, name: '波纹',       cat: '扭曲' },
    { id: 16, name: '液化',       cat: '扭曲' },
    { id: 17, name: '鱼眼',       cat: '扭曲' },
    { id: 18, name: '故障扭曲',   cat: '扭曲' },
    { id: 19, name: 'RGB 分离',   cat: '故障' },
    { id: 20, name: '信号干扰',   cat: '故障' },
    { id: 21, name: '数字噪点',   cat: '故障' },
    { id: 22, name: '光晕扫过',   cat: '光效' },
    { id: 23, name: '镜头光斑',   cat: '光效' },
    { id: 24, name: '曝光闪烁',   cat: '光效' },
    { id: 25, name: '圆形擦除',   cat: '几何' },
    { id: 26, name: '百叶窗',     cat: '几何' },
    { id: 27, name: '马赛克分割', cat: '几何' },
    { id: 28, name: '像素化',     cat: '几何' },
    { id: 29, name: '粒子消散',   cat: '粒子' },
    { id: 30, name: '烟雾过渡',   cat: '粒子' },
    { id: 31, name: '光点聚散',   cat: '粒子' },
    { id: 32, name: '胶片闪烁',   cat: '复古' },
    { id: 33, name: '老电影划痕转场', cat: '复古' }
  ];

  const RATIOS = [
    { id: '9:16', w: 9, h: 16 }, { id: '16:9', w: 16, h: 9 },
    { id: '1:1', w: 1, h: 1 },   { id: '3:4', w: 3, h: 4 },
    { id: '4:3', w: 4, h: 3 },   { id: '2.35:1', w: 235, h: 100 }
  ];

  const CURVE_PRESETS = ['自定义', '蒙太奇', '英雄时刻', '子弹时间', '跳跃', '闪进', '闪出'];

  /* 8 项基础调节（EffectType：1 亮度 2 对比 3 饱和 4 色温 5 色调 10 锐化 9 暗角 11 颗粒） */
  /* 调节 8 项：与 core/adjust.h 的 AdjustType 一一对应（type 字段即引擎枚举）。
     kind: 'slider' 是渲染器认的控件类型，缺了它就渲染不出来——别删。 */
  const ADJUST_BASE = [
    { key: 'brightness', name: '亮度',   icon: 'i-adjust',  kind: 'slider', type: 1,  min: -1, max: 1, step: 0.01, def: 0 },
    { key: 'contrast',   name: '对比度', icon: 'i-adjust',  kind: 'slider', type: 2,  min: 0,  max: 3, step: 0.01, def: 1 },
    { key: 'saturation', name: '饱和度', icon: 'i-palette', kind: 'slider', type: 3,  min: 0,  max: 3, step: 0.01, def: 1 },
    { key: 'temperature',name: '色温',   icon: 'i-palette', kind: 'slider', type: 4,  min: -1, max: 1, step: 0.01, def: 0 },
    { key: 'tint',       name: '色调',   icon: 'i-palette', kind: 'slider', type: 5,  min: -1, max: 1, step: 0.01, def: 0 },
    { key: 'sharpen',    name: '锐化',   icon: 'i-fx',      kind: 'slider', type: 10, min: 0,  max: 5, step: 0.05, def: 0 },
    { key: 'vignette',   name: '暗角',   icon: 'i-mask',    kind: 'slider', type: 9,  min: 0,  max: 1, step: 0.01, def: 0 },
    { key: 'grain',      name: '颗粒',   icon: 'i-adjust',  kind: 'slider', type: 11, min: 0,  max: 1, step: 0.01, def: 0 }
  ];

  /* ---------------- 面板规格 ---------------- */
  /* mode: single 单页 / dual 双页 / grid 宫格 */
  const SPEC = {
    /* ===== 剪辑 ===== */
    speed: {
      title: '变速', mode: 'dual',
      /* 第二页为区间曲线 */
      groups: [['speed', 'keeppitch']],
      curve: { presets: CURVE_PRESETS, unit: 'x' },
      pageNames: ['常规', '曲线']
    },
    volume: {
      title: '音量', mode: 'dual',
      groups: [['volume']],
      curve: { presets: null, unit: '%' },
      pageNames: ['常规', '区间']
    },
    opacity: {
      title: '不透明度', mode: 'dual',
      groups: [['opacity']],
      curve: { presets: null, unit: '%' },
      pageNames: ['常规', '区间']
    },
    denoise: {
      title: '降噪', mode: 'single',
      groups: [['denoiseon', 'denoiselv', 'sensitivity']]
    },
    freeze: {
      title: '定格', mode: 'single',
      groups: [['freezedur']],
      note: '定格为引擎侧静帧插入，需选中片段后使用'
    },
    reverse: {
      title: '倒放', mode: 'single',
      /* 引擎 nc_set_clip_reverse 只接受开关，无强度参数 */
      groups: [['reverseon']],
      note: '倒放会重建时间映射表，时长不变'
    },

    /* ===== 音频 ===== */
    fade: {
      title: '淡化', mode: 'dual',
      groups: [['fadein', 'fadeout']],
      curve: { presets: null, unit: '%' },
      pageNames: ['常规', '区间']
    },
    stem: {
      title: '人声分离', mode: 'single',
      groups: [['amplify', 'aiproc']],
      note: '本地推理（UVR-MDX），处理时长随素材长度增加'
    },
    record: {
      title: '录音', mode: 'single',
      groups: [['gain', 'aiproc']],
      note: '录音设备由系统权限控制，需在系统设置中授权麦克风'
    },
    /* 音乐/音效：素材库来自引擎已导入的音频轨（state.assets type=1）。
       联机版由 L3_HOST 注入真实列表；这里给的是静态兜底名。 */
    music: { title: '音乐', mode: 'grid', gridKind: 'audio', dyn: 'audio',
             grid: [{ id: 5, name: '夜幕铺底音乐', dur: 8.0, thumb: 'audio_sm/au_music.jpg' }] },
    sfx:   { title: '音效', mode: 'grid', gridKind: 'audio', dyn: 'audio',
             grid: [{ id: 6, name: '转场风声', dur: 0.9, thumb: 'audio_sm/au_sfx.jpg' }] },

    /* ===== 文字 ===== */
    newtext: {
      title: '新建文本', mode: 'single',
      groups: [['fontsize', 'textcolor', 'stroke', 'valign']]
    },
    subtitle: {
      title: '识别字幕', mode: 'single',
      groups: [['offset']],
      note: 'Whisper 本地识别；时长受会员额度限制'
    },
    lyrics: {
      title: '识别歌词', mode: 'single',
      groups: [['offset']],
      note: 'Whisper 本地识别'
    },
    ctrack: {
      title: '镜头追踪', mode: 'single',
      groups: [['trackmode', 'sensitivity', 'trackfollow']]
    },
    translate: {
      title: '字幕翻译', mode: 'single',
      groups: [['overwrite']],
      note: '翻译模型为本地推理，需先在「AI 模型」中下载'
    },
    tts: {
      title: '文本转语音', mode: 'single',
      groups: [['ttsspeed', 'pitch', 'aiproc']],
      note: '合成语音需遵守 AI 生成内容标识义务'
    },

    /* ===== 画中画 ===== */
    pipmix: {
      title: '混合模式', mode: 'single',
      groups: [['opacity']],
      note: '混合模式（正片叠底/滤色/叠加等）本版尚未接入引擎'
    },

    /* ===== 特效 ===== */
    fxscreen: { title: '画面特效', mode: 'grid', gridKind: 'effect', grid: EFFECTS.画面, then: 'fxparam' },
    fxperson: { title: '人物特效', mode: 'grid', gridKind: 'effect', grid: EFFECTS.人物, then: 'fxparam' },
    fxatmo:   { title: '氛围特效', mode: 'grid', gridKind: 'effect', grid: EFFECTS.氛围, then: 'fxparam' },
    fxmotion: { title: '动感特效', mode: 'grid', gridKind: 'effect', grid: EFFECTS.动感, then: 'fxparam' },
    fxparam: {
      title: '特效参数', mode: 'dual',
      groups: [['fxamt']],
      curve: { presets: null, unit: '' },
      pageNames: ['常规', '区间'],
      hidden: true   /* 不在一级二级里出现，仅宫格点选后进入 */
    },

    /* ===== 转场 ===== */
    transpreset: { title: '转场', mode: 'grid', gridKind: 'transition', grid: TRANSITIONS, then: 'transparam' },
    transparam: {
      title: '转场参数', mode: 'single',
      groups: [['transdur']],
      hidden: true
    },
    transkf: {
      title: '自定义转场', mode: 'dual',
      groups: [['transdur']],
      curve: { presets: null, unit: '' },
      pageNames: ['常规', '曲线']
    },

    /* ===== 滤镜 ===== */
    filterall: { title: '滤镜库', mode: 'grid', gridKind: 'filter', grid: FILTERS,
                 sameBase: true, then: 'filteramt' },
    filteramt: {
      title: '滤镜强度', mode: 'dual',
      groups: [['filteramt']],
      curve: { presets: null, unit: '' },
      pageNames: ['常规', '区间'],
      hidden: true
    },

    /* ===== 调节 ===== */
    adj: {
      title: '调节', mode: 'dual',
      groups: [],        /* 由 ADJUST_BASE 动态生成 8 行 */
      dynamic: 'adjust',
      curve: { presets: null, unit: '' },
      pageNames: ['常规', '区间']
    },

    /* ===== 比例 ===== */
    ratio: {
      title: '比例', mode: 'grid', gridKind: 'ratio', grid: RATIOS, then: 'ratiocustom'
    },
    ratiocustom: {
      title: '自定义比例', mode: 'single',
      groups: [['ratioW', 'ratioH']],
      hidden: true
    },

    /* ===== 背景 ===== */
    bgcolor: { title: '背景颜色', mode: 'single', groups: [['bgcolor', 'opacity']] },
    bgblur:  { title: '背景模糊', mode: 'single', groups: [['blur']] },
    bgstyle: {
      title: '画布样式', mode: 'grid', gridKind: 'style',
      grid: [
        { id: 1, name: '无' }, { id: 2, name: '纸纹' }, { id: 3, name: '布纹' },
        { id: 4, name: '磨砂' }, { id: 5, name: '网格' }, { id: 6, name: '渐变' }
      ]
    },

    /* ===== 蒙版 ===== */
    maskshape: {
      title: '蒙版', mode: 'grid', gridKind: 'mask', grid: MASKS, then: 'maskparam'
    },
    chroma: {
      title: '色度键', mode: 'single',
      groups: [['chromakey', 'chromain', 'chromaout', 'spill', 'chromaq', 'chromaon']],
      note: '按颜色抠除背景，用于绿幕 / 蓝幕素材'
    },
    maskparam: {
      title: '蒙版参数', mode: 'single',
      groups: [['posx', 'posy', 'maskw', 'maskh', 'maskrot', 'radius', 'feather', 'invert']],
      hidden: true
    },
    maskfeather: {
      title: '蒙版羽化', mode: 'single', groups: [['feather', 'invert']]
    },

    /* ===== AI ===== */
    refclip: {
      title: '参考仿剪', mode: 'single',
      groups: [['originality']],
      note: '仅分析节奏/转场/字幕样式/调色/构图，不复刻画面内容'
    },
    depth: {
      title: '深度图', mode: 'single',
      groups: [['depthmode', 'depthamt']],
      note: '单目深度估计为本地推理，需下载模型'
    }
  };

  window.L3_SPEC = {
    controls: C,
    spec: SPEC,
    adjustBase: ADJUST_BASE,
    curvePresets: CURVE_PRESETS,
    filters: FILTERS,
    effects: EFFECTS,
    masks: MASKS,
    transitions: TRANSITIONS,
    ratios: RATIOS
  };
})();
