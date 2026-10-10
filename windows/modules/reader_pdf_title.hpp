#pragma once
// Source PDF title for the Windows Reader. The Mac ReaderDocument names a PDF
// book by PDFDocument.documentAttributes[.titleAttribute] (the document
// information dictionary's /Title), else by the file name. Windows.Data.Pdf
// exposes no document information, so this bounded, read-only reader locates
// the trailer /Info dictionary itself and decodes /Title exactly as PDFKit
// renders it (tools/reader_pdf_title_reference.swift exports the oracle):
//   - classic cross-reference tables, incremental updates (/Prev, newest
//     first), hybrid files (/XRefStm) and cross-reference streams (Flate, PNG
//     predictors), objects in object streams, one indirect /Title reference;
//   - literal strings (escapes, octal, line continuations, CR/CRLF -> LF) and
//     hex strings (odd digit padded), duplicate keys (the last one wins);
//   - UTF-16BE or UTF-16LE with BOM (unpaired surrogates -> U+FFFD, an odd
//     trailing byte dropped), otherwise PDFDocEncoding with undefined codes
//     rendered as U+0020 (a UTF-8 BOM is not special, as in PDFKit).
// Everything else yields no title and the caller keeps the file name: a
// missing, empty or non-string /Title, a double indirection, an /Encrypt
// trailer (PDFKit decrypts owner-password-only files; this reader does not)
// and a damaged cross-reference (PDFKit repairs some of those). Reads at most
// a few bounded windows of the file; never a page, never the whole file.
#include "modules/reader_zip.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace endfield::modules {
inline constexpr std::size_t readerPDFTitleMaximumBytes = 1024 * 1024; // raw /Title string bytes
// Worker only; throws ReaderCancelled when `cancelled` turns true and passes
// read failures of the byte source through. A structural problem is nullopt.
std::optional<std::u16string> readerPDFTitle(ReaderBytes&, const ReaderCancel& cancelled = {});
// PDF text-string decoding alone (PDF 32000-1 7.9.2.2 as PDFKit applies it).
std::u16string readerPDFTextString(std::span<const std::uint8_t>);
} // namespace endfield::modules
