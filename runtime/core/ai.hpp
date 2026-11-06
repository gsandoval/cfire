#pragma once
// runtime/core/ai.hpp — Unified AI, Embeddings, Document Extraction, Vision/OCR & MCP SDK (`ext:ai`)
//
// Sub-headers:
//   - ai/vectors.hpp   : Core AI types, SIMD vector similarity, chunking & local feature embeddings
//   - ai/documents.hpp : Zero-dependency DEFLATE/ZIP, PDF, OOXML (.docx/.pptx/.xlsx), ODF & EPUB parsers
//   - ai/vision.hpp    : PNG/BMP/PPM decoders, visual HOG/dHash embeddings, Apple Vision / Tesseract OCR
//   - ai/client.hpp    : Unified LLM Client (OpenAI/Anthropic/Gemini/Ollama/Apple) & MCP JSON-RPC Server

#include "ai/vectors.hpp"
#include "ai/documents.hpp"
#include "ai/vision.hpp"
#include "ai/client.hpp"
