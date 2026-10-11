# Stages the EndfieldHUD Windows release artifacts from the install tree
# (cmake --install --component EndfieldHUD: EndfieldHUD.exe, fonts/, resources/)
# into OUT, never publishing anything:
#
#   EndfieldHUD-<release>-build<N>-windows-x64-portable.zip   the install tree
#   EndfieldHUD-<release>-build<N>-windows-x64-setup.exe      per-user installer
#   EndfieldHUD-<release>-build<N>-windows-x64-symbols.zip    EndfieldHUD.pdb, when built
#   EndfieldHUD-<release>-build<N>-windows-x64-size-report.json/.md
#   EndfieldHUD-<release>-build<N>-windows-x64-SHA256SUMS.txt
#   EndfieldHUD-<release>-build<N>-windows-x64-release-notes.md   draft notes for a Windows release
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
# EHUD_STAGED_PORTABLE, EHUD_STAGED_REPORT, EHUD_STAGED_SUMS, EHUD_STAGED_NOTES
# and EHUD_STAGED_COMPLETE set.
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
if(NOT ehud_entries STREQUAL "EndfieldHUD.exe;fonts;resources")
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
set(ehud_sums "")
foreach(artifact IN ITEMS "${EHUD_STAGED_SETUP}" "${EHUD_STAGED_PORTABLE}" "${ehud_symbols}")
  if(artifact AND EXISTS "${artifact}")
    file(SHA256 "${artifact}" ehud_hash)
    get_filename_component(ehud_name "${artifact}" NAME)
    string(APPEND ehud_sums "${ehud_hash}  ${ehud_name}\n")
  endif()
endforeach()
set(EHUD_STAGED_SUMS "${OUT}/${ehud_stem}${ehud_suffix}-SHA256SUMS.txt")
file(WRITE "${EHUD_STAGED_SUMS}" "${ehud_sums}")

# 8. Release-notes draft for a Windows release (packaging/release-notes-template.md):
#    the artifact names, sizes and SHA-256 values filled in; never published here.
function(ehud_mebibytes file out)
  file(SIZE "${file}" bytes)
  math(EXPR hundredths "(${bytes} * 100 + 524288) / 1048576")
  math(EXPR whole "${hundredths} / 100")
  math(EXPR fraction "${hundredths} % 100")
  if(fraction LESS 10)
    set(fraction "0${fraction}")
  endif()
  set(${out} "${whole}.${fraction} MiB" PARENT_SCOPE)
endfunction()
set(EHUD_NOTES_VERSION "${ehud_release_plain}")
set(EHUD_NOTES_BUILD "${ehud_build}")
set(EHUD_NOTES_COMMIT "${ehud_commit}")
get_filename_component(EHUD_NOTES_SETUP "${EHUD_STAGED_SETUP}" NAME)
get_filename_component(EHUD_NOTES_PORTABLE "${EHUD_STAGED_PORTABLE}" NAME)
get_filename_component(EHUD_NOTES_SUMS "${EHUD_STAGED_SUMS}" NAME)
ehud_mebibytes("${EHUD_STAGED_SETUP}" EHUD_NOTES_SETUP_SIZE)
ehud_mebibytes("${EHUD_STAGED_PORTABLE}" EHUD_NOTES_PORTABLE_SIZE)
file(SHA256 "${EHUD_STAGED_SETUP}" EHUD_NOTES_SETUP_SHA256)
file(SHA256 "${EHUD_STAGED_PORTABLE}" EHUD_NOTES_PORTABLE_SHA256)
if(ehud_signed)
  set(EHUD_NOTES_SIGNING "The setup, `uninstall.exe` and `EndfieldHUD.exe` are signed by DDDuoDuo.")
else()
  set(EHUD_NOTES_SIGNING "These builds are not code-signed yet, so Windows SmartScreen may show a warning the first time a downloaded copy runs.")
endif()
set(EHUD_NOTES_DEVELOPER "")
if(NOT EHUD_STAGED_COMPLETE)
  set(EHUD_NOTES_DEVELOPER "> **Incomplete developer package. Do not publish:** the Mac-exported runtime inputs were missing and this setup refuses to install.\n\n")
endif()
set(EHUD_STAGED_NOTES "${OUT}/${ehud_stem}${ehud_suffix}-release-notes.md")
configure_file("${SOURCE}/packaging/release-notes-template.md" "${EHUD_STAGED_NOTES}" @ONLY NEWLINE_STYLE LF)
file(REMOVE_RECURSE "${ehud_stage}")
message(STATUS "EndfieldHUD release artifacts staged in ${OUT} (${ehud_stem}${ehud_suffix}; signed: ${ehud_signed})")
