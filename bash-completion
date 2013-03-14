#!/bin/bash

in_array() {
  local i
  for i in "${@:2}"; do
    [[ $1 = "$i" ]] && return
  done
}

_ponymix() {
  local flags='-h --help -c --card -d --device -t --devtype
               -N --notify --source --input --sink --output
               --sink-input --source-output -V --version'
  local types='sink sink-input source source-output'
  local verbs=(help defaults set-default list list-short
               list-cards list-cards-short get-volume set-volume
               get-balance set-balance adj-balance increase decrease
               mute unmute toggle is-muted move kill
               list-profiles list-profiles-short get-profile set-profile)
  local i=0 cur prev verb word devtype dev idx devices

  _get_comp_words_by_ref cur prev

  for word in "${COMP_WORDS[@]}"; do
    # find the verb
    if in_array "$word" "${verbs[@]}"; then
      verb=$word
      break
    fi

    case $word in
      --devtype|-t)
        # look ahead to next word
        if [[ ${COMP_WORDS[i+1]} ]]; then
          devtype=${COMP_WORDS[i+1]}
        fi
        ;;
      --devtype=*)
        devtype=${word#--devtype=}
        ;;
      -t*)
        devtype=${word#-t}
        ;;
      --sink|--output)
        devtype=sink
        ;;
      --source|--input)
        devtype=source
        ;;
      --sink-input|--source-output)
        devtype=${word#--}
        ;;
    esac
    (( ++i ))
  done

  case $prev in
    --card|-c)
      mapfile -t cards < <(\ponymix list-cards-short 2>/dev/null)
      local IFS=$'\n'
      COMPREPLY=($(compgen -W '$(printf "%s\n" "${cards[@]//\ /\\ }")' -- "$cur"))
      ;;
    --device|-d)
      while IFS=$'\t' read _ dev idx _; do
        devices+=("$dev" "$idx")
      done < <(\ponymix "--${devtype:-sink}" list-short 2>/dev/null)
      local IFS=$'\n'
      COMPREPLY=($(compgen -W '$(printf "%s\n" "${devices[@]//\ /\\ }")' -- "$cur"))
      ;;
    --devtype|-t)
      COMPREPLY=($(compgen -W '$types' -- "$cur"))
      ;;
  esac
  [[ $COMPREPLY ]] && return 0

  case $cur in
    -*)
      COMPREPLY=($(compgen -W '${flags[*]}' -- "$cur"))
      ;;
    *)
      [[ -z $verb ]] && COMPREPLY=($(compgen -W '${verbs[*]}' -- "$cur"))
      ;;
  esac
  [[ $COMPREPLY ]] && return 0

  case $word in
    '')
      COMPREPLY=($(compgen -W '${verbs[*]}' -- "$cur"))
      ;;
  esac
  [[ $COMPREPLY ]] && return 0

  case $verb in
    move)
      if [[ $devtype = sink?(-input) ]]; then
        while IFS=$'\t' read _ dev idx _; do
          devices+=("$dev" "$idx")
        done < <(\ponymix --sink list-short 2>/dev/null)
      elif [[ $devtype = source?(-output) ]]; then
        while IFS=$'\t' read _ dev idx _; do
          devices+=("$dev" "$idx")
        done < <(\ponymix --source list-short 2>/dev/null)
      fi
      if [[ $devices ]]; then
        local IFS=$'\n'
        COMPREPLY=($(compgen -W '$(printf "%s\n" "${devices[@]//\ /\\ }")' -- "$cur"))
      fi
      ;;
  esac

  return 0
}

complete -F _ponymix ponymix

# vim: set et ts=2 sw=2:
