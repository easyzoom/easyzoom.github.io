---
title: Vibe Coding
author: EASYZOOM
date: 2026/04/20 12:20
categories:
 - Vibe Coding
tags:
 - AI
 - Vibe Coding
 - Cursor
 - Claude Code
---

# Vibe Coding

## 前言

**C：** **Vibe Coding** 指以"意图驱动、模型代写、人类校准"为主的新型编码工作流——你描述目标、调控节奏、做关键决策，模型把大部分机械劳动顶过去。本册记录这种工作流在真实项目中的得失，而不是"AI 会不会取代程序员"的观点输出。

<!-- more -->

## 本册内容范围

计划围绕下列方向展开（随写作进度持续调整）：

- 工具生态：Cursor、Claude Code、Windsurf、Codex CLI、各类 IDE Agent
- Spec-Driven / Plan-Driven 工作流：先写清楚要做什么，再交给模型
- Prompt 复用：规则（rules）、技能（skills）、记忆（memory）的组织方式
- 代码审阅：怎么让模型产出**可 review** 的小 diff 而不是一大坨
- 长上下文管理：子任务、子 Agent、上下文裁剪
- 团队协作：多人共享的 rules 仓库、PR 模板、CI 联动
- 失败模式：过度自信、跑偏、幻觉 API、隐式删除

## 学习建议

- 小步提交、频繁回滚、把"人工 review"当成一等公民。
- 把每次"和模型一起写代码"的会话都看作**可以被提炼成 rule 或 skill** 的原料。
- 工具与模型更新极快，实际配置以文末时间背景为准。

::: tip 持续更新中

章节与示例会陆续补充；若你发现疏漏或内容已过时，欢迎评论交流。

:::
