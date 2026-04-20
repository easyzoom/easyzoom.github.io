import type { DefaultTheme } from 'vitepress';

export const nav: DefaultTheme.Config['nav'] = [
  {
    text: 'Linux & 嵌入式',
    items: [
      { text: 'Linux基础', link: '/courses/linux/index' },
      { text: 'Linux驱动开发', link: '/courses/linuxdev/index' },
      { text: 'Linux内核开发', link: '/courses/kernel/index' },
      { text: 'U-Boot开发', link: '/courses/uboot/index' },
      { text: 'MCU开发与实践', link: '/courses/mcu/index' },
      { text: 'FPGA学习与实践', link: '/courses/fpga/index' },
    ],
    activeMatch: '/courses/(linuxdev|linux|kernel|uboot|mcu|fpga)/',
  },
  {
    text: '编程语言',
    items: [
      { text: 'C语言', link: '/courses/c/index' },
      { text: 'C++', link: '/courses/cpp/index' },
      { text: 'Python', link: '/courses/python/index' },
      { text: 'Rust', link: '/courses/rust/index' },
      { text: 'Java', link: '/courses/java/index' },
      { text: 'Shell', link: '/courses/shell/index' },
      { text: '数据库', link: '/courses/database/index' },
    ],
    activeMatch: '/courses/(cpp|c|python|rust|java|shell|database)/',
  },
  {
    text: 'RTOS & 机器人',
    items: [
      { text: 'uC/OS', link: '/courses/ucos/index' },
      { text: 'FreeRTOS', link: '/courses/freertos/index' },
      { text: 'Zephyr', link: '/courses/zephyr/index' },
      { text: 'ROS2', link: '/courses/ros2/index' },
    ],
    activeMatch: '/courses/(ucos|freertos|zephyr|ros2)/',
  },
  {
    text: '板卡 & 工具',
    items: [
      {
        text: '板卡实践',
        items: [
          { text: 'NXP系列', link: '/courses/nxp/index' },
          { text: 'RK系列', link: '/courses/rk/index' },
          { text: 'NVIDIA系列', link: '/courses/nvidia/index' },
        ],
      },
      {
        text: 'DevOps & 工具',
        items: [
          { text: 'Docker', link: '/courses/docker/index' },
          { text: 'Git', link: '/courses/git/index' },
          { text: '开源库', link: '/courses/openlib/index' },
          { text: 'MySQL', link: '/courses/mysql/index' },
        ],
      },
    ],
    activeMatch: '/courses/(nxp|rk|nvidia|docker|git|openlib|mysql)/',
  },
  {
    text: 'AI',
    items: [
      { text: '基础与原理', link: '/courses/ai-basics/index' },
      { text: '智能体工具', link: '/courses/ai-agent/index' },
      { text: 'Vibe Coding', link: '/courses/vibe-coding/index' },
    ],
    activeMatch: '/courses/(ai-basics|ai-agent|vibe-coding)/',
  },
  {
    text: '随笔',
    items: [
      { text: '方案春秋志', link: '/categories/solutions/index' },
      { text: '工具四海谈', link: '/categories/tools/index' },
    ],
    activeMatch: '/categories/(tools|solutions)/',
  },
  {
    text: '更多',
    items: [
      { text: '归档', link: '/archives' },
      { text: '标签', link: '/tags' },
      { text: '关于知识库', link: '/about/index' },
      { text: '关于我', link: '/about/me' },
    ],
    activeMatch: '/archives|/about/|/tags',
  },
];