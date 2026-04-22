---
title: 切分Chunking：把文档变成可检索单元
author: EASYZOOM
date: 2026/04/22 21:10
categories:
 - AI基础与原理
tags:
 - AI
 - LLM
 - RAG
 - Chunking
---

# 切分 Chunking：把文档变成可检索单元

## 前言

**C：** RAG 里最"朴素"也最被低估的一环是 chunking。它在流水线最上游——**切错了，后面 embedding 再强、rerank 再贵都救不回来**。这篇把 chunking 的逻辑讲透：为什么要切、切多大、按什么切、以及最容易踩的坑。

<!-- more -->

## 一、为什么要切，不能整篇上

能不能整篇文档当成一个 chunk 丢进向量库？理论上可以，实务上三个问题：

1. **语义稀释**：一篇 5000 字的文章被平均成一个向量，"主题"被平均掉，检索相似度反而低；
2. **上下文预算**：LLM 一次只能吃几 K–几十 K token，一整篇文章塞进 prompt 会**把预算烧光**还放不下其他片段；
3. **定位不准**：用户问"密码怎么重置"，匹配到整篇《账号管理手册》没用，得指到**那一段**。

所以 chunking 的本质是：

> 把一个**文档级别的对象**，拆成**召回单元**——每个单元小到可以对答相关主题，大到保留足够语义。

## 二、目标：一个"好" chunk 长什么样

从检索系统的角度，chunk 要同时满足：

| 性质 | 为什么重要 | 违反时会发生什么 |
|---|---|---|
| **主题单一** | 向量能代表一件事 | 相似度被多主题平均掉 |
| **自包含** | 单独看能读懂 | 模型看了半截上下文答错 |
| **长度稳定** | 向量模型最优区间有限 | 有些太短信息不足，有些太长被截 |
| **有足够上下文** | 标题、章节、谁说的 | 检索到了也回答不出 |
| **保留结构元数据** | 后续过滤、引用、去重 | 无法按文档/作者/时间过滤 |

抽象一下，一个 chunk 在库里的结构通常是：

```json
{
  "id": "kb/hr/leave_policy.md#L120",
  "text": "员工每年享有 15 天带薪年假……",
  "metadata": {
    "source":    "kb/hr/leave_policy.md",
    "title":     "年假政策",
    "heading":   "三、年假天数",
    "author":    "HR-Team",
    "updated_at":"2026-03-15",
    "tokens":    312
  }
}
```

`text` 进 embedding；`metadata` 进过滤和 prompt 的"出处"字段。

## 三、常见的切分策略谱系

从**粗暴到智能**，五档：

```mermaid
flowchart LR
  a["① 定长字符切"] --> b["② 递归分隔符切"] --> c["③ 结构感知切\n(markdown/HTML/代码)"] --> d["④ 语义切\n(embedding/LLM 判断边界)"] --> e["⑤ 任务感知切\n(按 query 类型动态调整)"]
```

### 3.1 定长字符切

最简单：每 N 个字符切一段，带 overlap。

```python
def fixed_chunk(text: str, size: int = 800, overlap: int = 120):
    i, out = 0, []
    while i < len(text):
        out.append(text[i:i+size])
        i += size - overlap
    return out
```

- **优点**：实现一行，任何文本都能切；
- **缺点**：会在**句子中间**砍断——"……请在设置界面点击重置密" | "码按钮……"，检索时这一段语义就断了。

**结论**：只在**脏数据探路**或**演示**时用；上线别用。

### 3.2 递归分隔符切（RecursiveCharacterTextSplitter）

LangChain、LlamaIndex 的默认策略，核心思路：

> **先按"大"分隔符切；切出来的每段若仍超长，再按次一级分隔符切；递归到满足长度。**

分隔符的默认层级（中英文通用版）：

```python
separators = ["\n\n", "\n", "。", "！", "？", ".", "?", "!", "；", ";", "，", ",", " ", ""]
```

> **中文优先加全角标点**，否则会回落到按字符切——切出一堆句中段。

伪代码：

```python
def split(text, size, separators):
    if len(text) <= size:
        return [text]
    for sep in separators:
        if sep in text:
            parts = text.split(sep)
            out = []
            buf = ""
            for p in parts:
                if len(buf) + len(p) + len(sep) <= size:
                    buf = buf + sep + p if buf else p
                else:
                    out.append(buf); buf = p
            if buf: out.append(buf)
            # 任何还超长的子段继续递归
            return sum([split(x, size, separators[1:]) for x in out], [])
    # 最后兜底：硬切
    return fixed_chunk(text, size, size//8)
```

这是**生产上最常用的 baseline**。80% 的场景这一档够用。

### 3.3 结构感知切

**利用文档本身的结构**——Markdown / HTML / 代码有天然层级，别白白丢掉。

**Markdown** 例：按 `#`, `##`, `###` 切，把 heading 路径作为 metadata：

```python
# MarkdownHeaderTextSplitter (简化)
splitter = MarkdownHeaderTextSplitter(
    headers_to_split_on=[("#", "h1"), ("##", "h2"), ("###", "h3")],
    strip_headers=False,
)
chunks = splitter.split_text(md_text)
# 每个 chunk.metadata 自动带 {"h1":"员工手册","h2":"年假"}
```

这样检索到的 chunk**自带上下文路径**——模型知道它在讨论"员工手册 → 年假 → 三、年假天数"，不是飘在半空。

**HTML**：按 `<h1> <h2> <section> <article>` 切；用 BeautifulSoup 提取正文、去掉导航/页脚。

**代码**：用 `tree-sitter` 或 `ast` 按函数/类切——一个函数一个 chunk，保留 `file + symbol` 作 metadata：

```python
from langchain_text_splitters import Language, RecursiveCharacterTextSplitter
splitter = RecursiveCharacterTextSplitter.from_language(
    language=Language.PYTHON, chunk_size=800, chunk_overlap=100,
)
```

内置版用 Python 语法感知的分隔符（`def`、`class`、`\n\n`……）。

**表格 / PDF**：如果原文有表格，**千万别线性切**——表头会被切掉，剩下一堆纯数字毫无语义。常见做法：

- 把每行表格渲染成"键: 值"形式的短文本（每行一个 chunk）；
- 或整表渲染成 markdown 表格做一个 chunk + 每行一个 chunk 两份入库。

### 3.4 语义切（Semantic Chunking）

思路：用 embedding 衡量**相邻句子**的语义相似度，**相似度突降处**切一刀。

```python
import numpy as np
sents = split_sentences(text)
vecs  = embed(sents)
sims  = [cos(vecs[i], vecs[i+1]) for i in range(len(sents)-1)]

# 低于分位点的位置作为切分点
threshold = np.percentile(sims, 10)
cuts = [i+1 for i, s in enumerate(sims) if s < threshold]
chunks = group_by_cuts(sents, cuts)
```

**优点**：切在真正的"话题转换处"，chunk 主题更单一；
**缺点**：**慢**、**贵**（要先过一遍 embedding 才能切）、对短文档收益小。

**什么时候用**：长文档（论文、财报、法律条文）、领域文本、对召回质量敏感的场景。

### 3.5 任务感知切

**终极形态**：根据**要回答的问题类型**决定切分粒度。

- 做**事实问答** → 切小（句子/段落）；
- 做**文章总结** → 切大（章节）；
- 做**表格查询** → 按行切；
- 做**对话式客服** → 按 QA 对切。

落地一般不是"一次切完"，而是**多粒度入库**——同一篇文档**同时存**句子级 + 段落级 + 章节级，查询时根据 router 选粒度。

## 四、chunk_size 和 overlap 怎么选

两个最直观的参数，也是最多争论的：

### 4.1 chunk_size（每段 token 数）

经验区间：

| 场景 | 建议 chunk_size | 理由 |
|---|---|---|
| FAQ / 短文档 | 200–400 tok | 问题通常对短事实 |
| 通用知识库 | 400–800 tok | 既保语义又不太散 |
| 学术 / 长论文 | 800–1500 tok | 需要足够推理链 |
| 表格 / 代码 | 按**结构单元**，不按长度 | 结构优先 |

**为什么不是越大越好**：向量模型对过长文本有**平均化 / 截断**行为，表达力下降；另外每个 topK 片段都要进 prompt，chunk 太大 K 就得很小，召回多样性丢失。

**为什么不是越小越好**：太短（< 100 tok）的 chunk 往往**缺上下文**，检索到了也答不出；而且向量化成本按 chunk 数计，太碎成本暴涨。

### 4.2 chunk_overlap（相邻重叠）

为什么要 overlap：

> 避免关键信息正好被**切在边界**上——前半段只有"A 的前提"、后半段只有"A 的结论"，两段单独都不够用。

经验值：

- 通用：`overlap = 10%–20% × chunk_size`（chunk_size=800，overlap=100–160）；
- 结构化文本（markdown 按标题切）：**可以不要 overlap**，标题边界本身就是自然断点；
- 代码：一般**不要**或极小——function 边界是强边界。

**警告**：overlap 会让**库体积膨胀 10%–20%**、**召回时出现高度重复的 topK**（同一句话在相邻两个 chunk 里都有）。后者要在 dedup 阶段处理。

## 五、元数据：被低估的半个 chunk

很多人以为 chunk 就是一段 text，其实 **metadata 才决定了线上体验**。至少应该带：

```json
{
  "source":     "kb/hr/leave_policy.md",
  "title":      "年假政策",
  "heading_path":["员工手册","年假","三、年假天数"],
  "author":     "HR-Team",
  "created_at": "2025-09-01",
  "updated_at": "2026-03-15",
  "language":   "zh",
  "permission": ["all-staff"],
  "doc_type":   "policy",
  "token_count":312,
  "chunk_index":7,
  "chunk_total":12
}
```

带上之后你能做：

- **权限过滤**：`permission @> ['user:123']` 在检索前剪枝；
- **时间加权**：同主题倾向于更新的 chunk；
- **引用显示**：给用户看 `[员工手册/年假/三]`；
- **按文档去重**：top-K 里禁止同一 `source` 超过 2 条；
- **失效下架**：下线某文档，按 `source` 一键删除。

## 六、一个生产级 chunker 的参考实现

```python
from dataclasses import dataclass, field
from pathlib import Path

@dataclass
class Chunk:
    text:    str
    meta:    dict = field(default_factory=dict)

def chunk_markdown(path: Path) -> list[Chunk]:
    md    = path.read_text(encoding="utf-8")
    title = extract_h1(md) or path.stem

    # ① 先按标题切
    sections = split_by_markdown_headings(md)

    out: list[Chunk] = []
    for sec in sections:
        heading_path = sec.heading_path                 # ["员工手册","年假","..."]
        body         = sec.body

        # ② 再按递归分隔符切，目标 ~600 tok
        parts = recursive_split(body, size=600, overlap=80,
                                separators=["\n\n","\n","。","；",".",";"," ",""])

        for i, p in enumerate(parts):
            # ③ 为前 N 个字符加上标题前缀（关键！）
            prefixed = " > ".join(heading_path) + "\n\n" + p
            out.append(Chunk(
                text=prefixed,
                meta={
                    "source":       str(path),
                    "title":        title,
                    "heading_path": heading_path,
                    "chunk_index":  i,
                    "updated_at":   path.stat().st_mtime,
                    "token_count":  len(tokenize(p)),
                },
            ))
    return out
```

关键一行是 `prefixed = heading_path + p`——**把标题路径写进 chunk 的正文**。向量化的时候模型看到"员工手册 > 年假 > 三、年假天数 | 员工每年享有 15 天……"，比单看最后那一段命中率高得多。

## 七、常见翻车现场

### 7.1 "切出一堆很短的 chunk"

原因：递归分隔符里 `"。"` 优先级太高，遇到短句多的文本被碎成一句一段。
解决：把 `"。"` 往后挪，先让 `"\n\n"`、`"\n"` 消化大部分内容。

### 7.2 "中文检索总不准"

原因：用了默认英文 separators，没加中文全角标点 → 退化成按字符硬切。
解决：`separators = ["\n\n","\n","。","！","？","；","，",...]`。

### 7.3 "PDF 抽出的文本乱七八糟"

原因：PDF 原生排版是"按位置"而非"按段落"；页眉/页脚混入正文；双栏被串读。
解决：

- 用 `pdfplumber` / `pypdf` 再叠一层排版过滤；
- 扫描版 PDF 用 OCR（Tesseract / PaddleOCR）；
- 页眉页脚按**正则 / 出现频率**过滤掉。

### 7.4 "同一信息被检索到多次"

原因：overlap 让相邻 chunk 大量重复，top-K 里挤满"几乎一样的段落"。
解决：

- 检索后做**基于 Jaccard / MinHash** 的去重；
- 或同 `source` 取 top-N 限制。

### 7.5 "表格内容根本检索不到"

原因：表格被当普通文本切。
解决：

- PDF 表格用 `camelot` / `tabula` 单独抽；
- 每行渲成"列 1: 值 1; 列 2: 值 2" 的短句入库；
- 原表保留一份做展示/引用。

### 7.6 "chunk 自带标题，但检索回去给模型看只剩正文"

原因：chunking 时加了 heading prefix，但入库存的 `text` 里又剥掉了。
解决：**向量化用的文本 = 入库 `text` 字段 = 拼 prompt 用的文本**，三者始终一致，最省事也最不容易出错。

## 八、小结

- Chunking 是 RAG 流水线的**入口**，也是最容易省事省错的一环；
- 五档策略：**定长 → 递归 → 结构 → 语义 → 任务感知**，生产 baseline 是"结构感知 + 递归";
- `chunk_size` 看数据：FAQ 小、长文档大；`overlap` 通常 10%–20%；
- **元数据**决定线上体验——权限、时间、去重、引用全靠它；
- 中文 / PDF / 表格 / 代码都要**单独定制**，不要指望一套 splitter 打天下；
- 把 chunk 切好之前，别急着调 embedding 和 rerank——上游错了，下游无用功。

::: tip 延伸阅读

- [LangChain Text Splitters](https://python.langchain.com/docs/concepts/text_splitters/)
- [LlamaIndex SemanticChunker](https://docs.llamaindex.ai/en/stable/examples/node_parsers/semantic_chunking/)
- [Chunking Strategies for LLM Applications (Pinecone)](https://www.pinecone.io/learn/chunking-strategies/)
- 本册下一篇：`03-Embedding：把文本投影到语义空间`

:::
