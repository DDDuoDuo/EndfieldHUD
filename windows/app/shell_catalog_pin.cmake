# The catalog pin of the hidden traces and coverage runs (included by the
# run_*.cmake scripts): the SHA-256 that windows/inputs/shell/inputs.json
# records for profile-outline-catalog.ehscene. A different catalog given
# through EHUD_APP_INPUT_DIR fails the pin instead of being measured.
file(READ "${SOURCE}/inputs/shell/inputs.json" ehud_shell_inputs)
string(JSON ehud_shell_catalog GET "${ehud_shell_inputs}" catalog)
string(JSON ehud_catalog_sha GET "${ehud_shell_inputs}" files "${ehud_shell_catalog}" sha256)
