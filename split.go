package main

import (
	"encoding/binary"
	"log"
	"os"
)

func main() {
	for _, file := range os.Args[1:] {
		split := 0
		name := "a"
		data, err := os.ReadFile(file)
		if err != nil {
			log.Fatal(err)
		}
		i := 0
		for i < len(data) {
			n := int(binary.BigEndian.Uint16(data))
			n &^= 0x8000
			if i+2+n > 50_000_000 {
				if err := os.WriteFile(file+name, data[:i], 0666); err != nil {
					log.Fatal(err)
				}
				split++
				name = "abcdefghijkl"[split : split+1]
				data = data[i:]
				i = 0
				continue
			}
			i += 2 + n
		}

		if err := os.WriteFile(file+name, data, 0666); err != nil {
			log.Fatal(err)
		}
	}
}
