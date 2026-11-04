#pragma once
#include "core/data/data_store.hpp"
#include "core/notes_rich_text.hpp"
#include <functional>
#include <span>

namespace endfield::modules {
using ArchiveJson=ehud::data::Json;
// Source limits count Swift Characters, not bytes/UTF-16. The native owner
// supplies its installed Unicode implementation explicitly. No ASCII-only
// fallback, invented character count or normalization is hidden in this model.
struct ArchiveTextRules {
    std::function<std::size_t(std::string_view)> characters;
    std::function<std::string(std::string_view)> trimmed;
    std::function<std::string(std::string_view)> categoryNameKey;
    // NFC (String.precomposedStringWithCanonicalMapping): classified-document
    // passwords derive from this form. Optional for owners that never classify.
    std::function<std::string(std::string_view)> canonical{};
};
enum class ArchiveTemplate {journal,research};
struct ArchiveCategory {
    std::string id,name;double created{}; // Codable/Foundation epoch, not SQLite Unix time
    ArchiveJson::Object extra;
    // The NEW badge (1.3.0): set for categories made by "+", cleared once
    // opened. Stored only in the SQL viewed column (0 = NEW); never in JSON,
    // like the Mac CodingKeys (id, name, created).
    bool isNew{};
    bool operator==(const ArchiveCategory&)const=default;
};
// ArchiveCategory.nameIssue (ArchiveStore.swift:9, 27-45), for new and
// renamed names, on the trimmed name in this order: empty; more than 24
// Characters or 1024 UTF-8 bytes; a Cc/Zl/Zp scalar; a reserved name (the
// All and Uncategorized filter titles in every language, by name key); the
// name key of another category (`excluding` is the one being renamed).
// Names up to 80 Characters still load (validateArchiveCategory).
enum class ArchiveCategoryNameIssue {empty,tooLong,invalidCharacters,reserved,duplicate,unknownCategory};
enum class ArchiveCategoryRenameResult {renamed,unchanged,rejected};
inline constexpr std::size_t archiveMaximumCategoryNameLength=24;
std::optional<ArchiveCategoryNameIssue>archiveCategoryNameIssue(std::string_view name,std::span<const ArchiveCategory>,const ArchiveTextRules&,const std::optional<std::string>&excluding={});
bool archiveReservedCategoryName(std::string_view,const ArchiveTextRules&);
struct ArchiveEntry {
    std::string id;ArchiveTemplate type{ArchiveTemplate::journal};
    std::string title,body;double date{},modified{}; // Foundation epoch
    std::vector<ArchiveJson>media; // exact reference metadata only; never media bytes
    std::optional<std::string>categoryID;
    std::optional<core::notes::RichText>titleRichText,bodyRichText;
    core::notes::TextStyle titleStyle{.fontSize=17},bodyStyle;
    ArchiveJson::Object extra;
    bool operator==(const ArchiveEntry&)const=default;
};
struct ArchiveSummary {
    std::string id;ArchiveTemplate type{ArchiveTemplate::journal};std::string title;
    double date{};std::size_t mediaCount{};std::optional<std::string>categoryID;
    std::optional<ArchiveJson>thumbnail; // first attachment; may be omitted by aggregate budget
    // A classified tile shows the stamp and [ CLASSIFIED ], never the title
    // or media; ArchiveState withholds its thumbnail (1.3.0).
    bool isClassified{};
    bool operator==(const ArchiveSummary&)const=default;
};
struct ArchiveSnapshot {
    std::vector<ArchiveCategory>categories;std::vector<ArchiveSummary>entries;
    std::optional<ArchiveEntry>selected;
};
inline constexpr std::size_t archiveMaximumEntries=2000,archiveMaximumCategories=100,
    archiveMaximumBodyBytes=2*1024*1024,archiveMaximumPayloadBytes=20*1024*1024,
    archiveMaximumSummaryThumbnailBytes=16*1024*1024;
inline constexpr double archiveFoundationToUnix=978307200;
std::u16string archiveUTF16(std::string_view);
std::string archiveUTF8(std::u16string_view);
void validateArchiveMedia(const ArchiveJson&); // supports preserved Mac or explicit additive Windows locator
// A preserved Mac attachment (security-scoped bookmark): unusable on Windows
// until its file is chosen again (ArchivePreview "mediaRelink").
bool archiveMacMediaReference(const ArchiveJson&)noexcept;
void validateArchiveCategory(const ArchiveCategory&,const ArchiveTextRules&);
void validateArchiveEntry(const ArchiveEntry&,const ArchiveTextRules&);
void validateArchiveSummary(const ArchiveSummary&,const ArchiveTextRules&);
ArchiveCategory decodeArchiveCategory(const ArchiveJson&,const ArchiveTextRules&);
ArchiveEntry decodeArchiveEntry(const ArchiveJson&,const ArchiveTextRules&);
ArchiveJson encodeArchiveCategory(const ArchiveCategory&,const ArchiveTextRules&);
ArchiveJson encodeArchiveEntry(const ArchiveEntry&,const ArchiveTextRules&);
ArchiveSummary archiveSummary(const ArchiveEntry&);
std::size_t archiveThumbnailCost(const std::optional<ArchiveJson>&);
ArchiveCategory archiveLegacyCategory(ArchiveTemplate); // deterministic v1 migration IDs only
} // namespace endfield::modules
