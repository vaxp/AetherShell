# aetheridle
set -l all_events timeout before-sleep after-resume lock unlock idlehint
set -l cmd_events before-sleep after-resume lock unlock
set -l time_events idlehint timeout

complete -c aetheridle --arguments "$all_events"
complete -c aetheridle --condition "__fish_seen_subcommand_from $cmd_events" --require-parameter
complete -c aetheridle --condition "__fish_seen_subcommand_from $time_events" --exclusive

complete -c aetheridle -s h --description 'show help'
complete -c aetheridle -s d --description 'debug'
complete -c aetheridle -s w --description 'wait for command to finish'
