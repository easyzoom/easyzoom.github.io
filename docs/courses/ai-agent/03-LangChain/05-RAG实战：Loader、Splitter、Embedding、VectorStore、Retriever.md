---
title: RAG实战：Loader、Splitter、Embedding、VectorStore、Retriever
author: EASYZOOM
date: 2026/04/22 19:40
categories:
 - 智能体工具
tags:
 - AI
 - Agent
 - LangChain
 - RAG
 - Embedding
 - Vector Store
---

# RAG 实战：Loader、Splitter、Embedding、VectorStore、Retriever

## 前言

**C：** RAG（Retrieval-Augmented Generation）是 LLM 应用里**最实用**的一种模式：把自家文档、数据库、代码库作为"外挂记忆"喂给模型。LangChain 把这条流水线拆成**五个抽象**——**Loader / Splitter / Embedding / VectorStore / Retriever**。这一篇把这五件套和"怎么拼成一条 RAG chain"讲清楚，给你一套**可直接抄的生产骨架**。

<!-- more -->

## 一、心智模型：RAG 的五件套

```mermaid
flowchart LR
  subgraph ingest["离线：构建索引"]
    src["原始数据\n(PDF/MD/DB/URL)"]
    load["Loader"]
    split["Splitter"]
    emb1["Embedding"]
    vs["VectorStore\n(Chroma/Qdrant/PGVector)"]
    src --> load --> split --> emb1 --> vs
  end

  subgraph online["在线：回答问题"]
    q["用户问题"]
    emb2["Embedding"]
    retr["Retriever"]
    prm["Prompt"]
    llm["LLM"]
    ans["答案"]
    q --> emb2 --> retr --> prm --> llm --> ans
    vs --> retr
  end
```

五件套的分工：

| 组件 | 干什么 | 输入 → 输出 |
| -- | -- | -- |
| **Loader** | 把外界数据读成 LangChain 的 `Document` | `path/URL/...` → `list[Document]` |
| **Splitter** | 把大 Document 切成小 chunk | `list[Document]` → `list[Document]` |
| **Embedding** | 把文本变成向量 | `str` → `list[float]` |
| **VectorStore** | 向量索引 + 查询 | `Documents + vectors` → 召回 |
| **Retriever** | 抽象"给我 query 拿相关文档" | `str` → `list[Document]` |

记住这五个框框——剩下的就是往框框里填具体实现。

## 二、`Document`：LangChain 里的文本单元

```python
from langchain_core.documents import Document

Document(
    page_content="LangChain 1.0 于 2025 年 10 月发布...",
    metadata={"source":"/docs/langchain.md", "page": 3, "type":"md"},
)
```

两个字段：

- `page_content`：**正文**，embedding 只算这部分；
- `metadata`：**任意 dict**，用来做过滤、做引用展示、给模型看来源。

**原则**：**一切有用的坐标信息**（source、page、chunk_id、作者、时间、语言、tags）都塞进 metadata——以后过滤、引用、监控都离不开它。

## 三、Loader：读进来

LangChain 把大量第三方数据源适配成了 Loader。真实项目里最常用的几类：

### 3.1 文件系统

```python
from langchain_community.document_loaders import (
    TextLoader,
    UnstructuredMarkdownLoader,
    PyPDFLoader,
    DirectoryLoader,
)

docs = TextLoader("/docs/readme.txt", encoding="utf-8").load()
docs = UnstructuredMarkdownLoader("/docs/guide.md").load()
docs = PyPDFLoader("/docs/whitepaper.pdf").load()

# 递归扫目录
docs = DirectoryLoader(
    "/repo/docs",
    glob="**/*.md",
    loader_cls=UnstructuredMarkdownLoader,
    show_progress=True,
).load()
```

### 3.2 网页

```python
from langchain_community.document_loaders import WebBaseLoader

docs = WebBaseLoader("https://docs.langchain.com/llms.txt").load()
```

大规模抓取建议用专门的爬虫（Scrapy / httpx）先落盘，再走 `TextLoader`——别把爬虫逻辑塞进 RAG 管道。

### 3.3 SQL / NoSQL

```python
from langchain_community.document_loaders import SQLDatabaseLoader

docs = SQLDatabaseLoader(
    query="SELECT id, title, body FROM articles WHERE published=1",
    db=db,   # SQLAlchemy engine
    page_content_mapper=lambda row: row["body"],
    metadata_mapper=lambda row: {"id":row["id"], "title":row["title"]},
).load()
```

**选哪个 Loader 不重要**——重要的是拼出合格的 `Document`。实在没有适配器就**自己写三行**：

```python
docs = [Document(page_content=r["body"], metadata=dict(r)) for r in rows]
```

## 四、Splitter：切得好，后面全好

### 4.1 为什么要切

两个原因：

1. **Embedding 模型有 token 上限**（通常 512~8192 tokens），长文必须切；
2. **召回后塞进 prompt 也有上限**；chunk 过大 → 命中一块就占满上下文；chunk 过小 → 信息碎裂。

**经验值**：

- **中文**：500~1000 字一个 chunk，重叠 100~200 字；
- **英文 / 代码**：500~1000 tokens，重叠 100~200 tokens；
- **FAQ / 语义独立**：按段落/问答项本身切，无重叠。

### 4.2 `RecursiveCharacterTextSplitter`（最常用）

```python
from langchain_text_splitters import RecursiveCharacterTextSplitter

splitter = RecursiveCharacterTextSplitter(
    chunk_size=800,
    chunk_overlap=120,
    separators=["\n\n", "\n", "。", "！", "？", ".", "?", "!", " ", ""],
)

chunks = splitter.split_documents(docs)
```

**递归**的含义：先按最大粒度（`\n\n`）切；切完仍超 size 的块再按下一级切……直到块够小。这样**尽量保留自然边界**。

### 4.3 按 token 切（精确控字数）

```python
from langchain_text_splitters import RecursiveCharacterTextSplitter
import tiktoken

enc = tiktoken.encoding_for_model("gpt-4o-mini")

splitter = RecursiveCharacterTextSplitter.from_tiktoken_encoder(
    encoding_name="cl100k_base",
    chunk_size=400,      # tokens
    chunk_overlap=50,
)
```

### 4.4 结构化内容

- **Markdown**：`MarkdownHeaderTextSplitter`——**按标题层级**切，metadata 自动带 H1/H2/H3；
- **代码**：`RecursiveCharacterTextSplitter.from_language(Language.PYTHON, ...)`——语言感知的分隔符；
- **HTML**：`HTMLHeaderTextSplitter`；
- **语义**：`SemanticChunker`（实验）—— embedding 判相邻段的语义断点。

> 一条黄金规则：**切完一定打开几份 chunk 眼看一遍**。如果一个 chunk 看不懂是在说什么——说明切坏了，调 size/overlap/separators。

## 五、Embedding：文本变向量

### 5.1 常用 Embedding Provider

```python
from langchain_openai import OpenAIEmbeddings
emb = OpenAIEmbeddings(model="text-embedding-3-small")  # 1536 维、便宜
emb = OpenAIEmbeddings(model="text-embedding-3-large")  # 3072 维

from langchain_community.embeddings import HuggingFaceEmbeddings
emb = HuggingFaceEmbeddings(model_name="BAAI/bge-m3")       # 本地多语种强
emb = HuggingFaceEmbeddings(model_name="BAAI/bge-large-zh") # 中文优

from langchain_community.embeddings import OllamaEmbeddings
emb = OllamaEmbeddings(model="nomic-embed-text")
```

### 5.2 接口

```python
v1  = emb.embed_query("什么是 RAG？")           # -> list[float]
vs  = emb.embed_documents(["t1", "t2", "t3"])   # -> list[list[float]]
```

**注意**：很多模型**query 和 document 用不同的 prompt**（BGE 的 `query:` 前缀）。LangChain 的集成一般自动做了；自己实现的话要留意。

### 5.3 选型粗略

| 场景 | 推荐 |
| -- | -- |
| 纯英文，不想折腾 | `text-embedding-3-small` |
| 多语种（含中文）高质量 | `bge-m3` / `multilingual-e5-large` |
| 中文强 | `bge-large-zh-v1.5` / `acge-large-zh` |
| 完全离线 | 本地 `bge-small` + Ollama 兜底 |
| 向量维度小（省钱省存储）| `text-embedding-3-small` 可配 `dimensions=256` |

## 六、VectorStore：索引 + 查询

### 6.1 选型

| VectorStore | 优势 | 适合 |
| -- | -- | -- |
| **Chroma** | 零依赖，SQLite/DuckDB 底层 | 本地 / 小项目 |
| **FAISS** | 内存内、速度最快 | 小-中规模、可接受不持久化 |
| **Qdrant** | 生产级、含过滤 DSL | 中-大规模、需复杂过滤 |
| **Milvus** | 分布式、极致规模 | 超大规模 |
| **PGVector** | 在你的 Postgres 里 | 已用 PG、想少一个组件 |
| **Weaviate** | 模型内嵌、带 schema | 企业级、要 admin UI |
| **Elasticsearch (dense_vector)** | 混检（BM25 + 向量）天然 | 已用 ES 的团队 |

### 6.2 增删查

```python
from langchain_chroma import Chroma

vs = Chroma.from_documents(
    documents=chunks,
    embedding=emb,
    persist_directory="./.chroma",
    collection_name="docs",
)

# 纯向量检索
hits = vs.similarity_search("RAG 怎么做？", k=5)

# 带分数
hits = vs.similarity_search_with_score("RAG 怎么做？", k=5)

# 结合 metadata 过滤
hits = vs.similarity_search(
    "RAG 怎么做？",
    k=5,
    filter={"source": {"$in": ["/docs/rag.md", "/docs/vector.md"]}},
)

# 删除/更新
vs.delete(ids=["doc-123"])
vs.add_documents([Document(...)])
```

### 6.3 主流 Store 一套抽样代码

PGVector：

```python
from langchain_postgres import PGVector

vs = PGVector.from_documents(
    documents=chunks,
    embedding=emb,
    connection="postgresql+psycopg://user:pwd@host:5432/db",
    collection_name="docs",
    use_jsonb=True,
)
```

Qdrant：

```python
from langchain_qdrant import QdrantVectorStore

vs = QdrantVectorStore.from_documents(
    documents=chunks,
    embedding=emb,
    url="http://localhost:6333",
    collection_name="docs",
)
```

**所有 VectorStore 的接口几乎一致**——切换背后实现基本只改两行。

## 七、Retriever：RAG 的"查询门面"

Retriever 是一个 Runnable：`str → list[Document]`。**所有 VectorStore 都能变 Retriever**：

```python
retriever = vs.as_retriever(search_type="similarity", search_kwargs={"k": 5})

retriever.invoke("什么是 RAG？")
# -> list[Document]
```

### 7.1 `search_type`

- `similarity`：纯向量相似；
- `mmr`：**Maximal Marginal Relevance**，在相似里**增加多样性**，避免同主题刷屏；
- `similarity_score_threshold`：带阈值过滤，置信度低的直接扔。

```python
retriever = vs.as_retriever(
    search_type="mmr",
    search_kwargs={"k": 5, "fetch_k": 25, "lambda_mult": 0.3},
)
```

### 7.2 进阶 Retriever

| 类型 | 作用 |
| -- | -- |
| `MultiQueryRetriever` | 让 LLM 先把用户问题**拆/重写**成多个 query，合并召回 |
| `ContextualCompressionRetriever` | 召回后**再过滤**（LLM / rerank 模型）压缩无关内容 |
| `EnsembleRetriever` | 多路召回加权合并（向量 + BM25 等）|
| `ParentDocumentRetriever` | 索引**小子块**，命中后返回**大父块** |
| `SelfQueryRetriever` | 让 LLM 从自然语言中**抽出 metadata 过滤条件** |

实战最常用的是 **`EnsembleRetriever`（向量 + BM25）** 和 **`ContextualCompressionRetriever`（接 rerank）**。

Rerank 典型写法：

```python
from langchain.retrievers import ContextualCompressionRetriever
from langchain_cohere import CohereRerank
# 或任意 rerank 模型的 wrapper

reranker = CohereRerank(model="rerank-multilingual-v3.0", top_n=5)
retriever = ContextualCompressionRetriever(
    base_retriever=vs.as_retriever(search_kwargs={"k": 25}),
    base_compressor=reranker,
)
```

"**粗召回 25 条，rerank 后留 5 条**"是性价比极高的组合。

## 八、完整 RAG chain：用 LCEL 拼起来

```python
from langchain_openai import ChatOpenAI, OpenAIEmbeddings
from langchain_chroma import Chroma
from langchain_core.prompts import ChatPromptTemplate
from langchain_core.output_parsers import StrOutputParser
from langchain_core.runnables import RunnablePassthrough

emb   = OpenAIEmbeddings(model="text-embedding-3-small")
vs    = Chroma(persist_directory="./.chroma", embedding_function=emb)
retr  = vs.as_retriever(search_type="mmr", search_kwargs={"k":5})
llm   = ChatOpenAI(model="gpt-4o-mini", temperature=0)

def format_docs(docs):
    return "\n\n".join(
        f"[{i}] {d.metadata.get('source','?')}\n{d.page_content}"
        for i, d in enumerate(docs, 1)
    )

prompt = ChatPromptTemplate.from_messages([
    ("system",
     "你是一个文档问答助手。严格基于下面【上下文】回答；"
     "如上下文没答案就说'资料中未提及'。"
     "在答案末尾用 [1][2] 的形式给出引用编号。\n\n"
     "【上下文】\n{context}"),
    ("user", "{question}"),
])

rag = (
    {"context": retr | format_docs,
     "question": RunnablePassthrough()}
    | prompt | llm | StrOutputParser()
)

print(rag.invoke("LangChain 1.0 相比旧版有什么变化？"))
```

把这段抄下来，换成你家的向量库、改 system prompt，就能跑起来。

## 九、"带引用来源"升级版

真实场景里**仅给答案不够**，还要**列出引用**。用 `RunnableParallel` 同时产出答案 + 来源：

```python
from langchain_core.runnables import RunnableParallel

rag_with_sources = RunnableParallel(
    {"answer":  rag,
     "sources": retr | RunnableLambda(lambda docs: [
         {"source": d.metadata.get("source"),
          "snippet": d.page_content[:200]} for d in docs])}
)

out = rag_with_sources.invoke("LangChain 1.0 有什么变化？")
out["answer"]   # 模型答复
out["sources"]  # 前端可高亮的源数据
```

**一次检索、两份产出**。用 `astream_events` 还能在前端**边写答案边渲染引用列表**。

## 十、常见工程问题

### 10.1 **重复入库**怎么办

最简做法：给 `Document` 一个**稳定的 id**（内容 hash / 数据库主键），入库时以 id 幂等更新：

```python
chunks_with_ids = [
    Document(page_content=d.page_content,
             metadata={**d.metadata, "doc_id": sha1(d.page_content)})
    for d in chunks
]
vs.add_documents(chunks_with_ids, ids=[d.metadata["doc_id"] for d in chunks_with_ids])
```

更工程化：用 `langchain.indexes.SQLRecordManager` 做**增量同步**——新/改/删一致性都有。

### 10.2 **文档更新**后怎么清老版本

用一个**版本字段**（`version` 或 `source_mtime`）在 metadata；入库前先按 source 过滤老 id 删除：

```python
vs.delete(filter={"source": path})   # 各 VectorStore 语法不同
vs.add_documents(new_chunks)
```

### 10.3 **中文召回效果差**

依次排查：

1. **Embedding 模型**是不是中文友好（`text-embedding-3-small` 在中文上**不如** `bge-large-zh`）；
2. **Splitter** 的 separators 有没有中文标点；
3. **chunk_size/overlap** 是不是太大——中文切 1500 字往往一块塞了多话题；
4. 有没有**BM25 混检** —— 中文词形变化少，BM25 贡献度比英文大；
5. 有没有 **rerank**。

### 10.4 **召回准了但模型答得烂**

问题通常不在检索，在 **prompt**：

- 没明确"**只能基于上下文**"；
- 没告诉模型**怎么处理找不到**；
- 没要求**引用编号**——模型会"发挥"；
- 上下文**太多**（> 20 条）—— 不如**精选 5 条**再让模型读。

### 10.5 **延迟太高**

典型瓶颈：

- Embedding（**query** 一次一般 50~200ms）——本地模型 / 批量摊平；
- 向量检索——大多数 Store 在几百万条量级下 < 50ms，不是瓶颈；
- LLM 生成——**流式**输出 + 少传上下文；
- Rerank——本地 rerank 比云更省延迟。

## 十一、什么时候**不**用 RAG

- 数据**小到能塞进上下文**（几百 KB）—— 直接塞进 system 更省事；
- 问答是**算术 / 代码执行 / 精确 DB 查询** —— **用工具（SQL tool / 计算器）**，不用 RAG；
- 数据**高频变化且有严格一致性** —— 走在线工具查原始系统；RAG 异步刷会滞后。

## 十二、小结

- RAG 五件套：**Loader / Splitter / Embedding / VectorStore / Retriever**，LangChain 各有抽象；
- **Document** = `page_content + metadata`，metadata 是后面过滤/引用/监控的命根；
- **Splitter** 优先 `RecursiveCharacterTextSplitter`，中文 500~1000 字 + 100~200 重叠是稳妥起点；
- **Embedding** 选型：英文 `3-small`，多语种 `bge-m3`，中文 `bge-large-zh`；
- **VectorStore** 切换成本极低，按规模/运维选；
- **Retriever** 的升级阶梯：similarity → mmr → Ensemble + BM25 → Rerank；
- **RAG chain** 用 LCEL 拼：`{context + question} | prompt | llm | parser`；
- **生产化**关注：幂等入库、增量同步、版本清理、带引用返回、延迟分档。

::: tip 延伸阅读

- [Retrievers 文档](https://python.langchain.com/docs/concepts/retrievers/)
- [`SQLRecordManager` 增量索引](https://python.langchain.com/docs/how_to/indexing/)
- [`MultiQueryRetriever` / `ParentDocumentRetriever` 用法](https://python.langchain.com/docs/how_to/#retrievers)
- 下一篇：`06-LangGraph 与 create_agent`

:::
