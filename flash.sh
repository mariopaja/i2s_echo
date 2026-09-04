#!/bin/bash

# List of available boards
boards=(
  "stm32n6570_dk/stm32n657xx/fsbl"
  "zal_smart_endpoint/mimxrt1064/aam"
)

echo "Select a board:"
select board in "${boards[@]}"; do
  if [[ -n "$board" ]]; then
    echo "You selected: $board"
    break
  else
    echo "Invalid selection. Try again."
  fi
done

west build -p -b "$board"

if [[ $? -ne 0 ]]; then
  echo "Build failed. Exiting."
  exit 1
fi

west flash
