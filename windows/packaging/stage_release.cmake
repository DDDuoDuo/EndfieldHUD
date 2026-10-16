# Stages the EndfieldHUD Windows release artifacts from the install tree
# (cmake --install --component EndfieldHUD: EndfieldHUD.exe,
# THIRD-PARTY-NOTICES.txt, fonts/, resources/) into OUT, never publishing anything:
#
#   EndfieldHUD-<release>-build<N>-windows-x64-portable.zip   the install tree
#   EndfieldHUD-<release>-build<N>-windows-x64-setup.exe      per-user installer
#   EndfieldHUD-<release>-build<N>-windows-x64-SHA256SUMS.txt  exactly the setup and the portable zip
#   EndfieldHUD-<release>-build<N>-windows-x64-symbols.zip    EndfieldHUD.pdb, when built (never uploaded)
#   EndfieldHUD-<release>-build<N>-windows-x64-symbols-SHA256SUMS.txt   its checksum
#   EndfieldHUD-<release>-build<N>-windows-x64-size-report.json/.md
#   EndfieldHUD-<release>-build<N>-windows-x64-release-notes.md   windows/RELEASE-NOTES.md
#       with its SHA-256 cells filled in: the draft for the GitHub release
#
# Only the setup, the portable zip and SHA256SUMS are uploaded to a release.
#
# A package whose resources do not verify (Mac-exported runtime inputs not
# configured) is refused unless ALLOW_INCOMPLETE, and then every artifact is
# named "-incomplete" and its setup refuses to install outside isolated tests.
# Signing runs only with DDDuoDuo's certificate (SIGN_SHA1 + SIGN_TIMESTAMP);
# otherwise the artifacts are reported as unsigned.
#
# Usage: cmake -DBUILD=<build> -DCONFIG=Release -DSOURCE=<windows/> -DOUT=<dir>
#   -DPACKER=<package_payload.exe> -DSTUB=<EndfieldHUD-Setup-stub.exe>
#   -DUNINSTALLER=<uninstall.exe> [-DSYMBOLS=<EndfieldHUD.pdb>] [-DALLOW_INCOMPLETE=ON]
#   [-DREQUIRE_COMPLETE=ON] [-DSIGN_SHA1=<thumbprint> -DSIGN_TIMESTAMP=<url>] -P stage_release.cmake
# When included from another script it leaves EHUD_STAGED_SETUP,
# EHUD_STAGED_PORTABLE, EHUD_STAGED_REPORT, EHUD_STAGED_SUMS,
# EHUD_STAGED_SYMBOLS_SUMS (empty without symbols), EHUD_STAGED_NOTES and
# EHUD_STAGED_COMPLETE set.
cmake_minimum_required(VERSION 3.25)
foreach(required BUILD CONFIG SOURCE OUT PACKER STUB UNINSTALLER)
  if(NOT ${required})
    message(FATAL_ERROR "stage_release: ${required} is required")
  endif()
endforeach()
function(ehud_stage_run what)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "stage_release: ${what} failed (${result}):\n${output}\n${errors}")
  endif()
  set(ehud_stage_output "${output}" PARENT_SCOPE)
endfunction()

file(READ "${SOURCE}/source-authority.json" ehud_authority)
string(JSON ehud_release GET "${ehud_authority}" release)
string(JSON ehud_build GET "${ehud_authority}" build)
string(JSON ehud_commit GET "${ehud_authority}" commit)
string(REGEX REPLACE "^v" "" ehud_release_plain "${ehud_release}")
set(ehud_stem "EndfieldHUD-${ehud_release_plain}-build${ehud_build}-windows-x64")
set(ehud_stage "${OUT}/stage")

# Previous outputs of this staging folder only.
file(REMOVE_RECURSE "${ehud_stage}")
file(MAKE_DIRECTORY "${OUT}")
file(GLOB ehud_previous "${OUT}/EndfieldHUD-*-windows-x64*")
if(ehud_previous)
  file(REMOVE ${ehud_previous})
endif()

# 1. The runtime install tree.
ehud_stage_run("Installing the EndfieldHUD component" "${CMAKE_COMMAND}" --install "${BUILD}" --config "${CONFIG}"
  --prefix "${ehud_stage}/EndfieldHUD" --component EndfieldHUD)
file(GLOB ehud_entries RELATIVE "${ehud_stage}/EndfieldHUD" "${ehud_stage}/EndfieldHUD/*")
list(SORT ehud_entries)
if(NOT ehud_entries STREQUAL "EndfieldHUD.exe;THIRD-PARTY-NOTICES.txt;fonts;resources")
  message(FATAL_ERROR "stage_release: unexpected install tree entries: ${ehud_entries}")
endif()
file(GLOB_RECURSE ehud_forbidden "${ehud_stage}/EndfieldHUD/*.pdb" "${ehud_stage}/EndfieldHUD/*.ilk" "${ehud_stage}/EndfieldHUD/*_tests.exe"
  "${ehud_stage}/EndfieldHUD/*.gif" "${ehud_stage}/EndfieldHUD/*.mp4" "${ehud_stage}/EndfieldHUD/*.mov" "${ehud_stage}/EndfieldHUD/*.tar.gz")
if(ehud_forbidden)
  message(FATAL_ERROR "stage_release: development or documentation files in the install tree: ${ehud_forbidden}")
endif()

# 2. The package must verify (resources/resources.json SHA-256 pins).
execute_process(COMMAND "${ehud_stage}/EndfieldHUD/EndfieldHUD.exe" --verify-package RESULT_VARIABLE ehud_verified
  OUTPUT_VARIABLE ehud_verify_report ERROR_VARIABLE ehud_verify_errors)
set(EHUD_STAGED_COMPLETE ON)
if(NOT ehud_verified EQUAL 0)
  if(NOT ALLOW_INCOMPLETE OR REQUIRE_COMPLETE)
    message(FATAL_ERROR "stage_release: the EndfieldHUD package does not verify (${ehud_verified}):\n${ehud_verify_report}\n${ehud_verify_errors}")
  endif()
  set(EHUD_STAGED_COMPLETE OFF)
  message(WARNING "stage_release: staging an INCOMPLETE developer package (its setup refuses to install):\n${ehud_verify_report}")
endif()
set(ehud_suffix "")
set(ehud_incomplete_flag "")
if(NOT EHUD_STAGED_COMPLETE)
  set(ehud_suffix "-incomplete")
  set(ehud_incomplete_flag "--incomplete")
endif()
file(COPY_FILE "${UNINSTALLER}" "${ehud_stage}/uninstall.exe")

# 3. Optional Authenticode signing with DDDuoDuo's certificate.
set(ehud_signed OFF)
set(ehud_signed_flag "")
if(SIGN_SHA1)
  if(NOT SIGN_TIMESTAMP)
    message(FATAL_ERROR "stage_release: signing needs SIGN_TIMESTAMP (an RFC 3161 timestamp server)")
  endif()
  file(GLOB ehud_signtools "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64/signtool.exe")
  if(NOT ehud_signtools)
    message(FATAL_ERROR "stage_release: signtool.exe (Windows SDK) was not found")
  endif()
  list(SORT ehud_signtools COMPARE NATURAL ORDER DESCENDING)
  list(GET ehud_signtools 0 ehud_signtool)
  function(ehud_sign file)
    ehud_stage_run("Signing ${file}" "${ehud_signtool}" sign /fd sha256 /sha1 "${SIGN_SHA1}" /tr "${SIGN_TIMESTAMP}" /td sha256 "${file}")
    ehud_stage_run("Verifying the signature of ${file}" "${ehud_signtool}" verify /pa "${file}")
  endfunction()
  ehud_sign("${ehud_stage}/EndfieldHUD/EndfieldHUD.exe")
  ehud_sign("${ehud_stage}/uninstall.exe")
  set(ehud_signed ON)
  set(ehud_signed_flag "--signed")
endif()

# 4. Portable zip (the install tree under one EndfieldHUD/ folder).
set(EHUD_STAGED_PORTABLE "${OUT}/${ehud_stem}${ehud_suffix}-portable.zip")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${EHUD_STAGED_PORTABLE}" --format=zip EndfieldHUD WORKING_DIRECTORY "${ehud_stage}"
  RESULT_VARIABLE ehud_zipped ERROR_VARIABLE ehud_zip_errors)
if(NOT ehud_zipped EQUAL 0)
  message(FATAL_ERROR "stage_release: creating the portable zip failed: ${ehud_zip_errors}")
endif()

# 5. Setup: stub + verified payload (install tree + uninstall.exe), then the
#    setup's own reader verifies it.
set(EHUD_STAGED_SETUP "${OUT}/${ehud_stem}${ehud_suffix}-setup.exe")
ehud_stage_run("Packing the setup" "${PACKER}" pack --tree "${ehud_stage}/EndfieldHUD" --add "uninstall.exe=${ehud_stage}/uninstall.exe"
  --stub "${STUB}" --out "${EHUD_STAGED_SETUP}" --release "${ehud_release}" --build "${ehud_build}" --commit "${ehud_commit}" ${ehud_incomplete_flag})
message(STATUS "${ehud_stage_output}")
if(ehud_signed)
  ehud_sign("${EHUD_STAGED_SETUP}")
endif()
ehud_stage_run("Verifying the setup payload" "${EHUD_STAGED_SETUP}" --verify-payload)
message(STATUS "${ehud_stage_output}")

# 6. Developer symbols, kept out of the consumer package.
set(ehud_artifacts "--artifact" "setup=${EHUD_STAGED_SETUP}" "--artifact" "portable=${EHUD_STAGED_PORTABLE}")
set(ehud_symbols "")
if(SYMBOLS AND EXISTS "${SYMBOLS}")
  set(ehud_symbols "${OUT}/${ehud_stem}${ehud_suffix}-symbols.zip")
  get_filename_component(ehud_symbols_dir "${SYMBOLS}" DIRECTORY)
  get_filename_component(ehud_symbols_name "${SYMBOLS}" NAME)
  ehud_stage_run("Archiving symbols" "${CMAKE_COMMAND}" -E chdir "${ehud_symbols_dir}" "${CMAKE_COMMAND}" -E tar cf "${ehud_symbols}" --format=zip "${ehud_symbols_name}")
  list(APPEND ehud_artifacts "--artifact" "symbols=${ehud_symbols}")
endif()

# 7. Size report and checksums.
set(EHUD_STAGED_REPORT "${OUT}/${ehud_stem}${ehud_suffix}-size-report.json")
ehud_stage_run("Writing the size report" "${PACKER}" report --tree "${ehud_stage}/EndfieldHUD" --add "uninstall.exe=${ehud_stage}/uninstall.exe"
  --release "${ehud_release}" --build "${ehud_build}" --commit "${ehud_commit}" --json "${EHUD_STAGED_REPORT}"
  --markdown "${OUT}/${ehud_stem}${ehud_suffix}-size-report.md" ${ehud_artifacts} ${ehud_incomplete_flag} ${ehud_signed_flag})
message(STATUS "${ehud_stage_output}")
# SHA256SUMS lists exactly the files uploaded to a release (the Windows
# updater checks a downloaded setup against it). The symbols zip is never
# uploaded: its checksum has a file of its own (and a row in the size report).
set(ehud_sums "")
foreach(artifact IN ITEMS "${EHUD_STAGED_SETUP}" "${EHUD_STAGED_PORTABLE}")
  file(SHA256 "${artifact}" ehud_hash)
  get_filename_component(ehud_name "${artifact}" NAME)
  string(APPEND ehud_sums "${ehud_hash}  ${ehud_name}\n")
endforeach()
set(EHUD_STAGED_SUMS "${OUT}/${ehud_stem}${ehud_suffix}-SHA256SUMS.txt")
file(WRITE "${EHUD_STAGED_SUMS}" "${ehud_sums}")
set(EHUD_STAGED_SYMBOLS_SUMS "")
if(ehud_symbols)
  file(SHA256 "${ehud_symbols}" ehud_hash)
  get_filename_component(ehud_name "${ehud_symbols}" NAME)
  set(EHUD_STAGED_SYMBOLS_SUMS "${OUT}/${ehud_stem}${ehud_suffix}-symbols-SHA256SUMS.txt")
  file(WRITE "${EHUD_STAGED_SYMBOLS_SUMS}" "${ehud_hash}  ${ehud_name}\n")
endif()

# 8. Release-notes draft: windows/RELEASE-NOTES.md, the reviewed text and the
#    only source of the release notes, with its "<!-- sha256 at release -->"
#    cells filled from the staged artifacts; never published here. Anything
#    the notes cannot describe (another release, an incomplete package, signed
#    artifacts) is marked "Do not publish" at the top of the draft.
file(READ "${SOURCE}/RELEASE-NOTES.md" ehud_notes)
string(REPLACE "\r\n" "\n" ehud_notes "${ehud_notes}")
set(ehud_notes_marks "")
foreach(expected IN ITEMS "windows-v${ehud_release_plain}" "${ehud_stem}-setup.exe" "${ehud_stem}-portable.zip" "${ehud_stem}-SHA256SUMS.txt")
  string(FIND "${ehud_notes}" "${expected}" ehud_found)
  if(ehud_found EQUAL -1)
    string(APPEND ehud_notes_marks "> **Do not publish:** windows/RELEASE-NOTES.md does not name `${expected}`. Update it for this release.\n\n")
  endif()
endforeach()
if(ehud_suffix)
  string(REPLACE "${ehud_stem}-" "${ehud_stem}${ehud_suffix}-" ehud_notes "${ehud_notes}")
endif()
set(ehud_notes_placeholder "<!-- sha256 at release -->")
foreach(artifact IN ITEMS "${EHUD_STAGED_SETUP}" "${EHUD_STAGED_PORTABLE}")
  file(SHA256 "${artifact}" ehud_hash)
  get_filename_component(ehud_name "${artifact}" NAME)
  string(REPLACE "| `${ehud_name}` | ${ehud_notes_placeholder} |" "| `${ehud_name}` | `${ehud_hash}` |" ehud_notes "${ehud_notes}")
endforeach()
string(FIND "${ehud_notes}" "${ehud_notes_placeholder}" ehud_found)
if(NOT ehud_found EQUAL -1)
  string(APPEND ehud_notes_marks "> **Do not publish:** a SHA-256 value in windows/RELEASE-NOTES.md has no staged artifact on its row.\n\n")
endif()
if(ehud_signed)
  string(FIND "${ehud_notes}" "not code-signed" ehud_found)
  if(NOT ehud_found EQUAL -1)
    string(APPEND ehud_notes_marks "> **Do not publish:** these artifacts are signed, but windows/RELEASE-NOTES.md describes unsigned builds.\n\n")
  endif()
endif()
if(NOT EHUD_STAGED_COMPLETE)
  string(PREPEND ehud_notes_marks "> **Incomplete developer package. Do not publish:** the Mac-exported runtime inputs were missing and this setup refuses to install.\n\n")
endif()
if(ehud_notes_marks)
  message(WARNING "stage_release: the release-notes draft is marked Do not publish:\n${ehud_notes_marks}")
  # Below the maintainer comment, above the title.
  string(FIND "${ehud_notes}" "\n# " ehud_title)
  if(ehud_title EQUAL -1)
    string(PREPEND ehud_notes "${ehud_notes_marks}")
  else()
    math(EXPR ehud_title "${ehud_title} + 1")
    string(SUBSTRING "${ehud_notes}" 0 ${ehud_title} ehud_notes_head)
    string(SUBSTRING "${ehud_notes}" ${ehud_title} -1 ehud_notes_tail)
    set(ehud_notes "${ehud_notes_head}${ehud_notes_marks}${ehud_notes_tail}")
  endif()
endif()
set(EHUD_STAGED_NOTES "${OUT}/${ehud_stem}${ehud_suffix}-release-notes.md")
file(WRITE "${EHUD_STAGED_NOTES}" "${ehud_notes}")
file(REMOVE_RECURSE "${ehud_stage}")
message(STATUS "EndfieldHUD release artifacts staged in ${OUT} (${ehud_stem}${ehud_suffix}; signed: ${ehud_signed})")
