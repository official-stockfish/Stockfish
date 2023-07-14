#!/bin/bash
# obtain and optionally verify Bench / signature
# if no reference is given, the output is deliberately limited to just the signature

error()
{
  echo "running bench for signature failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

# obtain

signature=`eval "$WINE_PATH ./stockfish bench 2>&1" | grep "Nodes searched  : " | awk '{print $4}'`

if [ $# -gt 0 ]; then
   # compare to given reference
   if [ "$1" != "$signature" ]; then
      if [ -z "$signature" ]; then
         echo "No signature obtained from bench. Code crashed or assert triggered ?"
      else
         echo "signature mismatch: reference $1 obtained: $signature ."
      fi
      exit 1
   else
      echo "signature OK: $signature"
   fi
else
   # just report signature
   echo $signature
fi
