#!/bin/bash

echo "Compilăm proiectul..."
make

if [ $? -eq 0 ]; then
    echo "Compilare reușită! Pornim Rummy..."
    ./rummy
else
    echo "Eroare la compilare! Verificați codul C."
fi
