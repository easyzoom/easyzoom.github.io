---
title: 智能体工具
author: EASYZOOM
date: 2026/04/20 12:10
categories:
 - 智能体工具
tags:
 - AI
 - Agent
 - MCP
 - Function Calling
---

# 智能体工具

## 前言

**C：** 智能体（Agent）不是"模型自己想干活"，而是**模型 + 工具 + 编排规则**的组合。本册关注**怎么把工具接得可控、可观测、可复现**，而不是堆砌框架名词。

<!-- more -->

## 本册内容范围

计划围绕下列方向展开（随写作进度持续调整）：

- Function Calling / Tool Use 的协议与常见坑
- MCP（Model Context Protocol）的工具、资源、提示三件套
- 主流 Agent 框架对比：LangChain / LangGraph / AutoGen / CrewAI / 自研路由
- 工作流编排：DAG、状态机、中断与恢复
- 多 Agent 协作：规划-执行-复核、主从、辩论
- 可观测性：Trace、replay、成本与延迟监控
- 安全边界：权限隔离、敏感操作确认、工具注入防护

## 学习建议

- 先用"一个模型 + 两三个工具"跑通端到端，再扩展到多 Agent。
- 把每次对话都当作潜在的回归测试样本保存下来。
- 工具协议迭代较快，注意区分 SDK 版本、规范版本与实际模型支持度。

::: tip 持续更新中

章节与示例会陆续补充；若你发现疏漏或内容已过时，欢迎评论交流。

:::
