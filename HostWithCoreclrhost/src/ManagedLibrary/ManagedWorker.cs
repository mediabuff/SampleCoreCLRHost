﻿using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;

namespace ManagedLibrary
{
    public class ManagedWorker
    {
        public delegate int ReportProgressFunction(int progress);

        // This test method doesn't actually do anything, it just takes some input parameters,
        // waits (in a loop) for a bit, invoking the callback function periodically, and
        // then returns a string version of the double[] passed in.
        [return: MarshalAs(UnmanagedType.LPStr)]
        public static string DoWork(string jobName, int iterations, int dataSize, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex=2)] double[] data, ReportProgressFunction reportProgressFunction)
        {
            for (int i = 1; i <= iterations; i++)
            {
                Console.ForegroundColor = ConsoleColor.Cyan;
                Console.WriteLine($"Beginning work iteration {i}");
                Console.ResetColor();

                Thread.Sleep(1000);

                var progressResponse = reportProgressFunction(i);
                Console.WriteLine($"Received response [{progressResponse}] from progress function");
            }

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"Work completed");
            Console.ResetColor();

            return $"Data received: {string.Join(", ", data.Select(d => d.ToString()))}";
        }
    }
}
