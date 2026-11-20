package main

import (
	"os"

	"golang.org/x/sys/windows"
)

func lockWebBake(path string) (*os.File, error) {
	file, err := os.OpenFile(path, os.O_CREATE|os.O_RDWR, 0644)
	if err != nil {
		return nil, err
	}
	flags := uint32(windows.LOCKFILE_EXCLUSIVE_LOCK | windows.LOCKFILE_FAIL_IMMEDIATELY)
	if err := windows.LockFileEx(windows.Handle(file.Fd()), flags, 0, 1, 0, new(windows.Overlapped)); err != nil {
		file.Close()
		return nil, err
	}
	return file, nil
}
