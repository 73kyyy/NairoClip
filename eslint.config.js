// ESLint 扁平配置（P-11 前端工程化）
//
// 目标不是「一次把历史代码全洗干净」，而是先把**会真出错的规则**打开：
//   · no-undef              —— 拼错变量名（历史上有过 e.selecet 这类）
//   · no-unused-vars        —— 死变量
//   · no-redeclare          —— 同一作用域重复声明
//   · eqeqeq                —— == 带来的隐式转换
// 风格类规则一律不开：这个项目是手写 DOM，缩进/引号靠人眼统一即可，
// 引入格式规则只会制造几百条噪声，掩盖真正的问题。

import globals from 'globals';

export default [
  {
    ignores: [
      'mobile/assets/static/**',   // 构建产物
      'dist/**',
      'models/**',
      'deps/**'
    ]
  },
  {
    files: ['mobile/assets/**/*.js', 'mobile/pages/**/*.js'],
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: 'script',
      globals: {
        ...globals.browser,
        // 项目全局：由 editor.js / nc-api.js 暴露
        NC_API: 'readonly',
        NCEngine: 'readonly',
        Engine: 'readonly',
        NC_ICONS: 'readonly'
      }
    },
    rules: {
      'no-undef': 'error',
      'no-unused-vars': ['warn', { args: 'none' }],
      'no-redeclare': 'error',
      'no-dupe-keys': 'error',
      'no-unreachable': 'error',
      eqeqeq: ['warn', 'smart']
    }
  }
];
