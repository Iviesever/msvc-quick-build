# MQB stable-v5 compatibility fragment for manual profile setup.
# The installer no longer overwrites user profiles; it merges an equivalent managed block only when needed.
function build {
    & "$HOME\bin\mqb.exe" @args
}
