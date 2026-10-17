#pragma once
// Source PDF title for the Windows Reader. The Mac ReaderDocument names a PDF
// book by PDFDocument.documentAttributes[.titleAttribute] (the document
// information dictionary's /Title), else by the file name. Windows.Data.Pdf
// exposes no document information, so this bounded, read-only reader locates
// the trailer /Info dictionary itself and decodes /Title exactly as PDFKit
// renders it (tools/reader_pdf_title_reference.sh exports the oracle from the
// unchanged ReaderDocument):
//   - classic cross-reference tables, incremental updates (/Prev, newest
//     first; a free entry does not hide an older definition), hybrid files
//     (/XRefStm) and cross-reference streams (Flate, PNG predictors), objects
//     in object streams, one indirect /Title reference, and only the newest
//     trailer's /Info;
//   - a damaged cross-reference rebuilt from the object headers and the last
//     classic "trailer" (files up to 64 MiB), as PDFKit repairs them;
//   - the standard security handler for files that open without a password
//     (owner-password-only): RC4 40-128 (R2/R3), AES-128 (R4 crypt filters,
//     /Identity strings, /EncryptMetadata false) and AES-256 (R5, R6), with
//     object streams decrypted before Flate;
//   - literal strings (escapes, octal, line continuations, CR/CRLF -> LF) and
//     hex strings (odd digit padded), duplicate keys (the last one wins);
//   - UTF-16BE or UTF-16LE with BOM (unpaired surrogates -> U+FFFD, an odd
//     trailing byte dropped), otherwise PDFDocEncoding with undefined codes
//     rendered as U+0020 (a UTF-8 BOM is not special, as in PDFKit).
// A missing, empty or non-string /Title, a double indirection, a file that
// needs a user password, or a file PDFKit cannot repair (no classic trailer,
// objects only inside object streams) yields no title, and the caller keeps
// the file name. Healthy files cost a few bounded windows, never a page and
// never the whole file.
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
