#!/bin/bash

# List of available boards
boards=(
  "stm32n6570_dk/stm32n657xx/fsbl"
  "stm32h573i_dk@C2/stm32h573xx"
  "stm32h573i_dk@D1/stm32h573xx"
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
