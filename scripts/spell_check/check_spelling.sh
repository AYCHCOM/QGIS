#!/usr/bin/env bash
###########################################################################
#    check_spelling.sh
#    ---------------------
#    Date                 : December 2016
#    Copyright            : (C) 2016 by Denis Rouzaud
#    Email                : denis.rouzaud@gmail.com
###########################################################################
#                                                                         #
#   This program is free software; you can redistribute it and/or modify  #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 2 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
###########################################################################

# -r: deactivate interactive mode to fix errors
# optional argument: list of files to be checked

DIR=$(git rev-parse --show-toplevel)/scripts/spell_check

AGIGNORE=${DIR}/.agignore

# GNU prefix command for mac os support (gsed, gsplit)
GP=
if [[ "$OSTYPE" =~ darwin* ]]; then
  GP=g
fi

# ARGUMENTS
INTERACTIVE=$( tty -s && echo YES || echo NO)
DEBUG=NO
OUTPUTLOG=""
while getopts ":rdl:" opt; do
  case $opt in
    r)
      INTERACTIVE=NO
      ;;
    d)
      DEBUG=YES
      ;;
    l)
      OUTPUTLOG=$OPTARG
      ;;
    \?)
      echo "Invalid option: -$OPTARG" >&2
      exit 1
      ;;
  esac
done
shift $(expr $OPTIND - 1)

if [ ! $# -eq 0 ]; then
  EXCLUDE=$(cat $AGIGNORE |${GP}sed -e 's/\s*#.*$//' -e '/^\s*$/d' | tr '\n' '|' |${GP}sed -e 's/|$//')
  INPUTFILES=$(echo $@ | tr -s '[[:blank:]]' '\n' | egrep -iv "$EXCLUDE" | tr '\n' ' ' )
  if [[ -z $INPUTFILES  ]]; then
    exit 0
  fi
  echo "Running spell check on files: $INPUTFILES"
else
  INPUTFILES="."
fi

# regex to find escape string
SPELLOKRX='(#\s*spellok|<!--\s*#\s*spellok\s*-->)$'

# split into several files to avoid too long regexes
SPLIT=6

${GP}split --number=l/$SPLIT --numeric-suffixes --suffix-length=2 --additional-suffix=~ ${DIR}/spelling.dat spelling

# global replace variables (dictionary)
declare -A GLOBREP_ALLFILES=()
declare -A GLOBREP_CURRENTFILE=()
declare -A GLOBREP_IGNORE=()

ERRORFOUND=NO

for I in $(seq -f '%02g' 0  $(($SPLIT-1)) ) ; do
  ( [[ "$INTERACTIVE" =~ YES ]] || [[ "$TRAVIS" =~ true ]] ) && printf "Progress: %d/%d\n" $I $SPLIT
  SPELLFILE=spelling$I~

  # if correction contains an uppercase letter and is the same as the error character wise, this means that the error is searched as a full word and case sensitive (not incorporated in a bigger one)
  CASEMATCH_FIXCASE=$(${GP}sed -rn '/^(\w+):\1(:\*)?$/Ip' $SPELLFILE | ${GP}sed -r 's/^(\w+):\1(:\*)?$/(\\b|_)\1(\\b|_)/I')
  REMAINS=$(          ${GP}sed -r  '/^(\w+):\1(:\*)?$/Id' $SPELLFILE)

  # for error or correction containing any non letter character (space, apostrophe) search is full word and case insensitive
  IGNORECASE_FIXSPECIALCHAR=$(echo "$REMAINS" | perl -ne "print if     /^(\w*[ '])*\w*:\w+(?(1)|[' ])/" | ${GP}sed -r 's/^(.*?):.*?(:\*)?$/(\\b|_)\1(\\b|_)/' )
  REMAINS=$(                  echo "$REMAINS" | perl -ne "print if not /^(\w*[ '])*\w*:\w+(?(1)|[' ])/")

  # This will try to look for misspelling within larger words.
  # Condition is hard to explain in words.
  # You can test it here: https://regex101.com/r/7kznVA/12
  # adding :* in spelling.dat willextra words that should not be checked in longer words ca
  # remove those in spelling.dat ending with :*
  # following can be checked in longer words case insensitively
  IGNORECASE_INWORD=$(echo "$REMAINS" | perl -ne 'print if     /^(\w)(\w)(\w)\w*(\w)(\w)(\w):(?:(?!\2\3\w|\w\1\2).)\w*?(?:(?!\5\6\w|\w\4\5)\w\w\w)$/' | cut -d: -f1 )
  REMAINS=$(          echo "$REMAINS" | perl -ne 'print if not /^(\w)(\w)(\w)\w*(\w)(\w)(\w):(?:(?!\2\3\w|\w\1\2).)\w*?(?:(?!\5\6\w|\w\4\5)\w\w\w)$/' | cut -d: -f1 )
  # Trying with the rest as whole words case insensitively
  IGNORECASE_WHOLEWORD=$(echo "$REMAINS" | ${GP}sed -r 's/^/(\\b|_)/; s/$/(\\b|_)/' )
  # or in camel case, case sensitively for word of at least 4 chars
  MATCHCASE_INWORD=$(echo "$REMAINS" | ${GP}sed -r '/^.{,3}$/d' | ${GP}sed -r 's/^(\w)(.*)/(\\b|_)(\l\1\L\2_|\u\1\U\2_|\u\1\L\2\U[_A-Z0-9])|\L[_a-z0-9]\u\1\L\2(\\b|\U[_A-Z0-9])|\L[_a-z0-9]\u\1\U\2\L(\\b|[_a-z0-9])/' )

  if [[ "$DEBUG" =~ YES ]]; then
    echo "*** FIX CASE (case sensitive) ***"
    echo "$CASEMATCH_FIXCASE"
    echo "*** SPECIAL CHAR (case insensitive) ***"
    echo "$IGNORECASE_FIXSPECIALCHAR"
    echo "*** IN WORD (case insensitive) ***"
    echo "$IGNORECASE_INWORD"
    echo "*** WHOLE WORDS (case insensitive) ***"
    echo "$IGNORECASE_WHOLEWORD"
    echo "*** IN WORD CAMELCASE (case sensitive) **"
    echo "$MATCHCASE_INWORD"
    echo "*****"
  fi

  CASEMATCH_FIXCASE=$(        echo "$CASEMATCH_FIXCASE"         | ${GP}sed -r '/^\s*$/d' | tr '\n' '\|' | ${GP}sed -r 's/\|$//')
  IGNORECASE_FIXSPECIALCHAR=$(echo "$IGNORECASE_FIXSPECIALCHAR" | ${GP}sed -r '/^\s*$/d' | tr '\n' '\|' | ${GP}sed -r 's/\|$//')
  IGNORECASE_INWORD=$(        echo "$IGNORECASE_INWORD"         | ${GP}sed -r '/^\s*$/d' | tr '\n' '\|' | ${GP}sed -r 's/\|$//')
  IGNORECASE_WHOLEWORD=$(     echo "$IGNORECASE_WHOLEWORD"      | ${GP}sed -r '/^\s*$/d' | tr '\n' '\|' | ${GP}sed -r 's/\|$//')
  MATCHCASE_INWORD=$(         echo "$MATCHCASE_INWORD"          | ${GP}sed -r '/^\s*$/d' | tr '\n' '\|' | ${GP}sed -r 's/\|$//')

  IGNORECASE=$(echo "(${IGNORECASE_FIXSPECIALCHAR}|${IGNORECASE_INWORD}|${IGNORECASE_WHOLEWORD})" |${GP}sed -r 's/\(\|/(/' |${GP}sed -r 's/\|\|/|/g' |${GP}sed -r 's/\|\)/)/')'(?!.*'"${SPELLOKRX}"')'
  CASEMATCH=$(echo "(${CASEMATCH_FIXCASE}|${MATCHCASE_INWORD})" |${GP}sed -r 's/\(\|/(/' |${GP}sed -r 's/\|\|/|/g' |${GP}sed -r 's/\|\)/)/')'(?!.*'"${SPELLOKRX}"')'

  FILE=$INPUTFILES  # init with input files (if ag is run with single file, file path is now in output)

  while read -u 3 -r LINE; do
    echo "$LINE"
    ERRORFOUND=YES
    NOCOLOR=$(echo "$LINE" | ${GP}sed -r 's/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g')
    if [[ "$NOCOLOR" =~ ^[[:alnum:]][[:alnum:]\/\._-]+$ ]]; then
      FILE=$NOCOLOR
      GLOBREP_CURRENTFILE=()
    fi
    if [[ "$NOCOLOR" =~ ^[0-9]+: ]]; then
      if [[ -z $FILE ]]; then
        echo "*** error: no file"
        exit 1
      fi
      NUMBER=$(echo "$NOCOLOR" | cut -d: -f1)
      ERRORLINE=$(echo "$NOCOLOR" | cut -d: -f2)
      ERROR=$(echo "$LINE" | ${GP}sed -r 's/^.*?\x1B\[30;43m(.*?)\x1B\[0m.*$/\1/')

      ERRORNOCOLOR=$(echo "$ERROR" | ${GP}sed -r 's/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g')
      if [[ "$ERRORNOCOLOR" =~ ^[[:digit:]]+: ]]; then
        echo "*** error: could not find error in $LINE" >&2
      else
        # if the error is not in IGNORECASE_INWORD, then it matched previous and next character (needs to remove them)
        # also make error small case and escape special chars: ( ) |
        ERRORSMALLCASE=$(echo ${ERROR,,} |${GP}sed -r 's/\(/\\(/g' |${GP}sed -r 's/\)/\\)/g' |${GP}sed -r 's/\|/\\|/g')
        if [[ ! "${ERRORSMALLCASE}" =~ $IGNORECASE_INWORD ]]; then
         if [[ ! -z $(ag --nonumbers --case-sensitive "^${ERRORSMALLCASE:1:-1}${ERRORSMALLCASE: -1}?:" scripts/spell_check/spelling.dat) ]]; then
           ERRORSMALLCASE=${ERRORSMALLCASE#?}
           ERROR=${ERROR#?}
         fi
         if [[ ! -z $(ag --nonumbers --case-sensitive "^${ERRORSMALLCASE::-1}:" scripts/spell_check/spelling.dat) ]]; then
           ERRORSMALLCASE=${ERRORSMALLCASE::-1}
           ERROR=${ERROR::-1}
         fi
        fi
        # get correction from spelling.dat
        CORRECTION=$(ag --nonumbers --case-sensitive "^${ERRORSMALLCASE}:" ${DIR}/spelling.dat | cut -d: -f2)

        if [[ -z $CORRECTION ]]; then
          echo "could not find correction for $ERROR" >&2
        else
          # Match case
          MATCHCASE="$ERROR:$CORRECTION"
          CORRECTIONCASE=$(echo "$MATCHCASE" | ${GP}sed -r 's/([A-Z]+):(.*)/\1:\U\2/; s/([A-Z][a-z]+):([a-z])/\1:\U\2\L/' | cut -d: -f2)

          if [[ ! -z $OUTPUTLOG ]]; then
            echo "$FILE $NUMBER $ERROR $CORRECTIONCASE" >> $OUTPUTLOG
          fi
          if [[ "$INTERACTIVE" =~ YES ]]; then
            # Skip global replace
            if [[ !  -z  ${GLOBREP_ALLFILES["$ERROR"]} ]]; then
              echo -e "replace \x1B[33m$ERROR\x1B[0m by \x1B[33m$CORRECTIONCASE\x1B[0m in \x1B[33m$FILE\x1B[0m"
              ${GP}sed -i -r "/${SPELLOKRX}/! s/$ERROR/$CORRECTIONCASE/g" $FILE
              continue
            elif [[ ( ! -z  ${GLOBREP_CURRENTFILE["$ERROR"]} ) || ( ! -z  ${GLOBREP_IGNORE["$ERROR"]} ) ]]; then
              echo "skipping occurrence"
              continue
            else
              # escape string
              SPELLOKSTR='//#spellok'
              if [[ "$FILE" =~ \.(txt|html|htm|dox)$ ]]; then
                SPELLOKSTR='<!--#spellok-->'
              fi
              if [[ "$FILE" =~ \.(h|cpp|sip)$ ]]; then
                if [[ "$ERRORLINE" =~ ^\s*(\/*\|\/\/) ]]; then
                  SPELLOKSTR='#spellok'
                fi
              fi
              if [[ "$FILE" =~ \.(py)$ ]]; then
                SPELLOKSTR='#spellok'
              fi
              SPELLOKSTR_ESC=$(echo "$SPELLOKSTR" | ${GP}sed -r 's/\//\\\//g')

              # Display menu
              echo "***"
              echo -e "Error found: \x1B[31m$ERROR\x1B[0m"
              echo -e "  r) \x1B[4mr\x1B[0meplace by \x1B[33m$CORRECTIONCASE\x1B[0m at line $NUMBER"
              echo -e "  f) replace all occurrences by \x1B[33m$CORRECTIONCASE\x1B[0m in current \x1B[4mf\x1B[0mile"
              echo -e "  a) replace all occurrences by \x1B[33m$CORRECTIONCASE\x1B[0m in \x1B[4ma\x1B[0mll files"
              echo -e "  p) a\x1B[4mp\x1B[0mpend \x1B[33m$SPELLOKSTR\x1B[0m at the end of the line $NUMBER to avoid spell check on this line"
              echo -e "  t) \x1B[4mt\x1B[0mype your own correction"
              echo -e "  c) skip and \x1B[4mc\x1B[0montinue"
              echo -e "  o) skip all \x1B[4mo\x1B[0mccurences and continue"
              echo -e "  e) \x1B[4me\x1B[0mxit"

              while read -n 1 n; do
                echo ""
                case $n in
                    r)
                      echo -e "replacing \x1B[33m$ERROR\x1B[0m by \x1B[33m$CORRECTIONCASE\x1B[0m in \x1B[33m$FILE\x1B[0m at line \x1B[33m$NUMBER\x1B[0m"
                      ${GP}sed -i "${NUMBER}s/$ERROR/$CORRECTIONCASE/g" $FILE
                      break
                      ;;
                    f)
                      GLOBREP_CURRENTFILE+=(["$ERROR"]=1)
                      echo -e "replacing \x1B[33m$ERROR\x1B[0m by \x1B[33m$CORRECTIONCASE\x1B[0m in \x1B[33m$FILE\x1B[0m"
                      ${GP}sed -i -r "/${SPELLOKRX}/! s/$ERROR/$CORRECTIONCASE/g" $FILE
                      break
                      ;;
                    a)
                      GLOBREP_CURRENTFILE+=(["$ERROR"]=1)
                      GLOBREP_ALLFILES+=(["$ERROR"]=1)
                      echo -e "replace \x1B[33m$ERROR\x1B[0m by \x1B[33m$CORRECTIONCASE\x1B[0m in \x1B[33m$FILE\x1B[0m"
                      ${GP}sed -i -r "/${SPELLOKRX}/! s/$ERROR/$CORRECTIONCASE/g" $FILE
                      break
                      ;;
                    p)
                      echo -e "appending \x1B[33m$SPELLOKSTR\x1B[0m to \x1B[33m$FILE\x1B[0m at line \x1B[33m$NUMBER\x1B[0m"
                      ${GP}sed -i "${NUMBER}s/\$/  $SPELLOKSTR_ESC/" $FILE
                      break
                      ;;
                    t)
                      echo "Enter the correction: "
                      read CORRECTION
                      MATCHCASE="$ERROR:$CORRECTION"
                      CORRECTIONCASE=$(echo "$MATCHCASE" | ${GP}sed -r 's/([A-Z]+):(.*)/\1:\U\2/; s/([A-Z][a-z]+):([a-z])/\1:\U\2\L/' | cut -d: -f2)
                      echo -e "replacing \x1B[33m$ERROR\x1B[0m by \x1B[33m$CORRECTIONCASE\x1B[0m in \x1B[33m$FILE\x1B[0m at line \x1B[33m$NUMBER\x1B[0m"
                      ${GP}sed -i "${NUMBER}s/$ERROR/$CORRECTIONCASE/g" $FILE
                      break
                      ;;
                    c)
                      break
                      ;;
                    o)
                      GLOBREP_IGNORE+=(["$ERROR"]=1)
                      break
                      ;;
                    e)
                      exit 1
                      ;;
                    *) invalid option;;
                esac
              done
            fi
          fi
        fi
      fi
      if [[ "$NOCOLOR" =~ ^\s*$ ]]; then
        FILE=""
      fi
    fi
  done 3< <(
    unbuffer ag --all-text --nopager --color-match "30;43" --numbers --nomultiline --ignore-case    -p $AGIGNORE "${IGNORECASE}" $INPUTFILES
    unbuffer ag --all-text --nopager --color-match "30;43" --numbers --nomultiline --case-sensitive -p $AGIGNORE "${CASEMATCH}"  $INPUTFILES
  )

  rm -f $SPELLFILE
done

if [[ "$ERRORFOUND" =~ YES ]]; then
  echo -e "\x1B[1msome errors have been found.\x1B[0m" >&2
  exit 1
else
  exit 0
fi