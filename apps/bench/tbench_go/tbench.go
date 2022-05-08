package main

import (
    "fmt"
    "runtime"
    "sync"
    "time"
)

var kMeasureRounds int = 10000000

func BenchUncontendedMutex() {
    var mu sync.Mutex
    var foo uint32 = 0
    for i := 0; kMeasureRounds > i; i++ {
        mu.Lock()
        foo++;
        mu.Unlock()
    }
}

func BenchYieldPingpong() {
    var wg sync.WaitGroup
    wg.Add(1)
    go func(wg *sync.WaitGroup) {
        for i := 0; (kMeasureRounds / 2) > i; i++ {
            runtime.Gosched()
        }
        wg.Done()
    }(&wg)
    for i := 0; (kMeasureRounds / 2) > i; i++ {
        runtime.Gosched()
    }
    wg.Wait()
}

func BenchCondvarPingpong() {
    var wg sync.WaitGroup
    var mu sync.Mutex
    var cv = sync.NewCond(&mu)
    var dir = false
    wg.Add(1)
    go func(wg *sync.WaitGroup) {
        for i := 0; (kMeasureRounds / 2) > i; i++ {
            cv.L.Lock()
            for dir {
                cv.Wait()
            }
            dir = true
            cv.Signal()
            cv.L.Unlock()
        }
        wg.Done()
    }(&wg)
    for i := 0; (kMeasureRounds / 2) > i; i++ {
        cv.L.Lock()
        for !dir {
            cv.Wait()
        }
        dir = false
        cv.Signal()
        cv.L.Unlock()
    }
    wg.Wait()
}

func BenchSpawnJoin() {
    var wg sync.WaitGroup
    for i := 0; kMeasureRounds > i; i++ {
        wg.Add(1)
        go func(wg *sync.WaitGroup) {
            wg.Done()
        }(&wg)
        wg.Wait()
    }
}

func main() {
    runtime.GOMAXPROCS(1)
    fmt.Printf("rounds: %d\n", kMeasureRounds)

    start0 := time.Now()
    BenchSpawnJoin()
    end0 := time.Now()
    fmt.Print("SpawnJoin: ")
    fmt.Println(end0.Sub(start0))

    start1 := time.Now()
    BenchUncontendedMutex()
    end1 := time.Now()
    fmt.Print("UncontendedMutex: ")
    fmt.Println(end1.Sub(start1))

    start2 := time.Now()
    BenchYieldPingpong()
    end2 := time.Now()
    fmt.Print("Yield: ")
    fmt.Println(end2.Sub(start2))

    start3 := time.Now()
    BenchCondvarPingpong()
    end3 := time.Now()
    fmt.Print("CondvarPingpong: ")
    fmt.Println(end3.Sub(start3))
}
