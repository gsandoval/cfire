#pragma once
// runtime/core/ai.hpp — Unified AI, Embeddings, Document Extraction, Vision/OCR, Agent & MCP SDK (`ext:ai`)
//
// Sub-headers:
//   - ai/types.hpp     : Core AI types (Message, Tool, ToolCall, ChatResponse, RegisteredTool) & constructors
//   - ai/vectors.hpp   : SIMD vector similarity, chunking & local feature embeddings
//   - ai/documents.hpp : Zero-dependency DEFLATE/ZIP, PDF, OOXML (.docx/.pptx/.xlsx), ODF & EPUB parsers
//   - ai/vision.hpp    : PNG/BMP/PPM decoders, visual HOG/dHash embeddings, Apple Vision / Tesseract OCR
//   - ai/client.hpp    : Unified LLM Client (OpenAI/Anthropic/Gemini/Ollama/Apple/Mock) & self-correcting extraction
//   - ai/agent.hpp     : Agent orchestrator (multi-turn tool loops & validation retries) & MCP JSON-RPC Server

#include "ai/types.hpp"
#include "ai/vectors.hpp"
#include "ai/documents.hpp"
#include "ai/vision.hpp"
#include "ai/client.hpp"
#include "ai/agent.hpp"
