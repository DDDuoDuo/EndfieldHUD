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
#
# Only the setup, the portable zip and SHA256SUMS are uploaded, as extra
# assets of the shared "v<release>" GitHub release (next to the Mac files).
#
# Three phases, so the release workflow (.github/workflows/windows-release.yml)
# can have EndfieldHUD.exe and uninstall.exe signed through SignPath between
# "tree" and "package", and the setup between "package" and "finish":
#   tree     installs the tree into OUT/stage with uninstall.exe, the setup
#            stub and the packer; copies the two programs to sign into
#            OUT/to-sign; archives the symbols. Cleans OUT first.
#   package  puts the signed programs (SIGNED, a folder) into the stage,
#            checks the signatures for MODE, verifies the package, writes the
#            portable zip and packs the setup. The setup stub stays unsigned:
#            the packer refuses a signed stub.
#   finish   puts the signed setup (SETUP) in place, checks its signature for
#            MODE and its payload, runs the install check (CHECKER), writes the
#            size report and SHA256SUMS from the final bytes, removes the stage.
# Without PHASE all three run in a row, unsigned (MODE none).
# MODE: none (unsigned), test (a signature is present: the SignPath test
# certificate is not trusted on the build machine) or release (a trusted
# signature whose signer is SIGNER).
#
# Usage: cmake [-DPHASE=tree|package|finish] -DSOURCE=<windows/> -DOUT=<dir>
#   tree:    -DBUILD=<build> -DCONFIG=Release -DPACKER=<package_payload.exe>
#            -DSTUB=<EndfieldHUD-Setup-stub.exe> -DUNINSTALLER=<uninstall.exe> [-DSYMBOLS=<EndfieldHUD.pdb>]
#   package: [-DMODE=none|test|release] [-DSIGNED=<folder>] [-DSIGNER=<name>]
#   finish:  [-DMODE=...] [-DSETUP=<signed setup.exe>] [-DSIGNER=<name>] [-DCHECKER=<packaging_installer_tests.exe>]
#   -P stage_release.cmake
cmake_minimum_required(VERSION 3.25)
if(NOT PHASE)
  set(ehud_phases tree package finish)
elseif(PHASE MATCHES "^(tree|package|finish)$")
  set(ehud_phases "${PHASE}")
else()
  message(FATAL_ERROR "stage_release: PHASE must be tree, package or finish")
endif()
if(NOT MODE)
  set(MODE none)
endif()
if(NOT MODE MATCHES "^(none|test|release)$")
  message(FATAL_ERROR "stage_release: MODE must be none, test or release")
endif()
set(ehud_required SOURCE OUT)
if("tree" IN_LIST ehud_phases)
  list(APPEND ehud_required BUILD CONFIG PACKER STUB UNINSTALLER)
endif()
if(NOT MODE STREQUAL "none")
  if("package" IN_LIST ehud_phases)
    list(APPEND ehud_required SIGNED)
  endif()
  if("finish" IN_LIST ehud_phases)
    list(APPEND ehud_required SETUP)
  endif()
  if(MODE STREQUAL "release")
    list(APPEND ehud_required SIGNER)
  endif()
elseif(SIGNED OR SETUP)
  message(FATAL_ERROR "stage_release: SIGNED and SETUP need MODE test or release")
endif()
foreach(required IN LISTS ehud_required)
  if(NOT ${required})
    message(FATAL_ERROR "stage_release: ${required} is required")
  endif()
endforeach()
# Paths given relative to the current folder (the release workflow passes
# OUT=release) made absolute: the zip steps run in other folders.
foreach(ehud_path SOURCE OUT BUILD PACKER STUB UNINSTALLER SYMBOLS SIGNED SETUP CHECKER)
  if(${ehud_path})
    cmake_path(ABSOLUTE_PATH ${ehud_path} BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
  endif()
endforeach()
# The tree phase empties OUT: only a staging folder of this script's own.
if("tree" IN_LIST ehud_phases AND IS_DIRECTORY "${OUT}")
  file(GLOB ehud_existing RELATIVE "${OUT}" "${OUT}/*")
  list(FILTER ehud_existing EXCLUDE REGEX "^(EndfieldHUD-.*-windows-x64-.*|stage|to-sign|checker)$")
  if(ehud_existing)
    message(FATAL_ERROR "stage_release: ${OUT} holds files this script did not stage (${ehud_existing}); give an empty or staging folder")
  endif()
endif()

function(ehud_stage_run what)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "stage_release: ${what} failed (${result}):\n${output}\n${errors}")
  endif()
  set(ehud_stage_output "${output}" PARENT_SCOPE)
endfunction()

# The Authenticode signature of a file, as MODE requires it.
function(ehud_check_signature file)
  set(ENV{EHUD_SIGNATURE_FILE} "${file}")
  # Called directly, not through ehud_stage_run: the script's semicolons
  # would split it into list items there. Without PSModulePath Windows
  # PowerShell builds its own module path: one inherited from PowerShell 7
  # (the workflow's shell) would load the wrong Microsoft.PowerShell.Security.
  set(ENV{PSModulePath} "")
  execute_process(COMMAND powershell -NoProfile -NonInteractive -Command
    "$s = Get-AuthenticodeSignature -LiteralPath $env:EHUD_SIGNATURE_FILE; $n = ''; if ($s.SignerCertificate) { $n = $s.SignerCertificate.GetNameInfo([System.Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false) }; Write-Output ('{0}|{1}' -f $s.Status, $n)"
    RESULT_VARIABLE result OUTPUT_VARIABLE signature ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "stage_release: reading the signature of ${file} failed (${result}):\n${errors}")
  endif()
  string(STRIP "${signature}" signature)
  string(REPLACE "|" ";" signature "${signature}")
  list(GET signature 0 status)
  list(LENGTH signature fields)
  set(signer "")
  if(fields GREATER 1)
    list(GET signature 1 signer)
  endif()
  if(MODE STREQUAL "none" AND NOT status STREQUAL "NotSigned")
    message(FATAL_ERROR "stage_release: ${file} should be unsigned, but its signature status is ${status} (${signer})")
  elseif(MODE STREQUAL "test" AND (signer STREQUAL "" OR NOT status MATCHES "^(Valid|UnknownError|NotTrusted)$"))
    message(FATAL_ERROR "stage_release: ${file} has no intact test signature (status ${status}, signer '${signer}')")
  elseif(MODE STREQUAL "release" AND (NOT status STREQUAL "Valid" OR NOT signer STREQUAL SIGNER))
    message(FATAL_ERROR "stage_release: ${file} has no trusted signature by '${SIGNER}' (status ${status}, signer '${signer}')")
  endif()
  message(STATUS "stage_release: ${file}: ${status} ${signer}")
endfunction()

file(READ "${SOURCE}/source-authority.json" ehud_authority)
string(JSON ehud_release GET "${ehud_authority}" release)
string(JSON ehud_build GET "${ehud_authority}" windowsBuild)
string(JSON ehud_commit GET "${ehud_authority}" commit)
string(REGEX REPLACE "^v" "" ehud_release_plain "${ehud_release}")
set(ehud_stem "EndfieldHUD-${ehud_release_plain}-build${ehud_build}-windows-x64")
set(ehud_stage "${OUT}/stage")
set(ehud_to_sign "${OUT}/to-sign")
set(ehud_portable "${OUT}/${ehud_stem}-portable.zip")
set(ehud_setup "${OUT}/${ehud_stem}-setup.exe")
set(ehud_symbols "${OUT}/${ehud_stem}-symbols.zip")

if("tree" IN_LIST ehud_phases)
  file(REMOVE_RECURSE "${OUT}")
  file(MAKE_DIRECTORY "${ehud_to_sign}")
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
  file(COPY_FILE "${UNINSTALLER}" "${ehud_stage}/uninstall.exe")
  file(COPY_FILE "${STUB}" "${ehud_stage}/EndfieldHUD-Setup-stub.exe")
  file(COPY_FILE "${PACKER}" "${ehud_stage}/package_payload.exe")
  file(COPY_FILE "${ehud_stage}/EndfieldHUD/EndfieldHUD.exe" "${ehud_to_sign}/EndfieldHUD.exe")
  file(COPY_FILE "${ehud_stage}/uninstall.exe" "${ehud_to_sign}/uninstall.exe")
  # Developer symbols, kept out of the consumer package; the symbols zip is
  # never uploaded, so its checksum has a file of its own.
  if(SYMBOLS AND EXISTS "${SYMBOLS}")
    get_filename_component(ehud_symbols_dir "${SYMBOLS}" DIRECTORY)
    get_filename_component(ehud_symbols_name "${SYMBOLS}" NAME)
    ehud_stage_run("Archiving symbols" "${CMAKE_COMMAND}" -E chdir "${ehud_symbols_dir}" "${CMAKE_COMMAND}" -E tar cf "${ehud_symbols}" --format=zip "${ehud_symbols_name}")
    file(SHA256 "${ehud_symbols}" ehud_hash)
    file(WRITE "${OUT}/${ehud_stem}-symbols-SHA256SUMS.txt" "${ehud_hash}  ${ehud_stem}-symbols.zip\n")
  endif()
  message(STATUS "stage_release: tree staged in ${ehud_stage}, programs to sign in ${ehud_to_sign}")
endif()

if("package" IN_LIST ehud_phases)
  if(NOT EXISTS "${ehud_stage}/EndfieldHUD/EndfieldHUD.exe" OR NOT EXISTS "${ehud_stage}/uninstall.exe")
    message(FATAL_ERROR "stage_release: no staged tree in ${ehud_stage} (run PHASE=tree first)")
  endif()
  if(SIGNED)
    foreach(ehud_program EndfieldHUD.exe uninstall.exe)
      if(NOT EXISTS "${SIGNED}/${ehud_program}")
        message(FATAL_ERROR "stage_release: the signed ${ehud_program} is missing from ${SIGNED}")
      endif()
    endforeach()
    file(COPY_FILE "${SIGNED}/EndfieldHUD.exe" "${ehud_stage}/EndfieldHUD/EndfieldHUD.exe")
    file(COPY_FILE "${SIGNED}/uninstall.exe" "${ehud_stage}/uninstall.exe")
  endif()
  ehud_check_signature("${ehud_stage}/EndfieldHUD/EndfieldHUD.exe")
  ehud_check_signature("${ehud_stage}/uninstall.exe")
  # The package that ships must verify (resources/resources.json SHA-256 pins).
  ehud_stage_run("Verifying the EndfieldHUD package" "${ehud_stage}/EndfieldHUD/EndfieldHUD.exe" --verify-package)
  file(REMOVE "${ehud_portable}" "${ehud_setup}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${ehud_portable}" --format=zip EndfieldHUD WORKING_DIRECTORY "${ehud_stage}"
    RESULT_VARIABLE ehud_zipped ERROR_VARIABLE ehud_zip_errors)
  if(NOT ehud_zipped EQUAL 0)
    message(FATAL_ERROR "stage_release: creating the portable zip failed: ${ehud_zip_errors}")
  endif()
  ehud_stage_run("Packing the setup" "${ehud_stage}/package_payload.exe" pack --tree "${ehud_stage}/EndfieldHUD"
    --add "uninstall.exe=${ehud_stage}/uninstall.exe" --stub "${ehud_stage}/EndfieldHUD-Setup-stub.exe" --out "${ehud_setup}"
    --release "${ehud_release}" --build "${ehud_build}" --commit "${ehud_commit}")
  message(STATUS "${ehud_stage_output}")
endif()

if("finish" IN_LIST ehud_phases)
  foreach(ehud_needed "${ehud_stage}/EndfieldHUD/EndfieldHUD.exe" "${ehud_portable}" "${ehud_setup}")
    if(NOT EXISTS "${ehud_needed}")
      message(FATAL_ERROR "stage_release: ${ehud_needed} is missing (run PHASE=package first)")
    endif()
  endforeach()
  if(SETUP)
    file(COPY_FILE "${SETUP}" "${ehud_setup}")
  endif()
  ehud_check_signature("${ehud_setup}")
  ehud_stage_run("Verifying the setup payload" "${ehud_setup}" --verify-payload)
  message(STATUS "${ehud_stage_output}")
  set(ehud_signed_flag "")
  set(ehud_payload_signing "unsigned")
  if(NOT MODE STREQUAL "none")
    set(ehud_signed_flag "--signed")
    set(ehud_payload_signing "signed")
  endif()
  if(NOT ehud_stage_output MATCHES ", complete, ${ehud_payload_signing}[\r\n]")
    message(FATAL_ERROR "stage_release: the setup payload is not complete and ${ehud_payload_signing}:\n${ehud_stage_output}")
  endif()
  if(CHECKER)
    ehud_stage_run("Installing and removing the setup (packaging_installer_tests --release)" "${CHECKER}" --release "${ehud_setup}")
    message(STATUS "${ehud_stage_output}")
  endif()

  set(ehud_artifacts "--artifact" "setup=${ehud_setup}" "--artifact" "portable=${ehud_portable}")
  if(EXISTS "${ehud_symbols}")
    list(APPEND ehud_artifacts "--artifact" "symbols=${ehud_symbols}")
  endif()
  ehud_stage_run("Writing the size report" "${ehud_stage}/package_payload.exe" report --tree "${ehud_stage}/EndfieldHUD"
    --add "uninstall.exe=${ehud_stage}/uninstall.exe" --release "${ehud_release}" --build "${ehud_build}" --commit "${ehud_commit}"
    --json "${OUT}/${ehud_stem}-size-report.json" --markdown "${OUT}/${ehud_stem}-size-report.md" ${ehud_artifacts} ${ehud_signed_flag})
  message(STATUS "${ehud_stage_output}")
  # SHA256SUMS lists exactly the files uploaded to a release (the Windows
  # updater checks a downloaded setup against it).
  set(ehud_sums "")
  foreach(artifact IN ITEMS "${ehud_setup}" "${ehud_portable}")
    file(SHA256 "${artifact}" ehud_hash)
    get_filename_component(ehud_name "${artifact}" NAME)
    string(APPEND ehud_sums "${ehud_hash}  ${ehud_name}\n")
  endforeach()
  file(WRITE "${OUT}/${ehud_stem}-SHA256SUMS.txt" "${ehud_sums}")
  file(REMOVE_RECURSE "${ehud_stage}" "${ehud_to_sign}")
  message(STATUS "EndfieldHUD release artifacts staged in ${OUT} (${ehud_stem}; signing: ${MODE})")
endif()
