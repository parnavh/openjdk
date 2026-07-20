package dev.parnavh.simpletest;

import java.util.*;
import java.util.stream.*;

/**
 * Test workload for JIT-Agent.
 * Designed to trigger Tier 4 (C2) compilation on several distinct method shapes:
 *   - tight numeric loops          → classic C2 target
 *   - virtual dispatch             → devirtualisation / inlining
 *   - string-heavy work            → intrinsic candidates
 *   - collection streaming         → lambda / invokedynamic
 *   - recursive with memoisation   → OSR candidate
 */
public class SimpleTest {

    // -------------------------------------------------------------------------
    // 1. Tight numeric loop — first thing C2 will grab
    // -------------------------------------------------------------------------
    static long sumArray(int[] arr) {
        long sum = 0;
        for (int v : arr) sum += v;
        return sum;
    }

    static double dotProduct(double[] a, double[] b) {
        double result = 0;
        for (int i = 0; i < a.length; i++) result += a[i] * b[i];
        return result;
    }

    // -------------------------------------------------------------------------
    // 2. Virtual dispatch — gives C2 something to devirtualise
    // -------------------------------------------------------------------------
    interface Scorer {
        double score(double x);
    }

    static class SigmoidScorer implements Scorer {

        public double score(double x) {
            return 1.0 / (1.0 + Math.exp(-x));
        }
    }

    static class TanhScorer implements Scorer {

        public double score(double x) {
            return Math.tanh(x);
        }
    }

    static double runScorer(Scorer s, int iterations) {
        double acc = 0;
        for (int i = 0; i < iterations; i++) acc += s.score(i * 0.001);
        return acc;
    }

    // -------------------------------------------------------------------------
    // 3. String work — StringBuilder, charAt, intrinsics
    // -------------------------------------------------------------------------
    static String buildCsv(int rows) {
        StringBuilder sb = new StringBuilder(rows * 20);
        for (int i = 0; i < rows; i++) {
            sb.append(i)
                .append(',')
                .append(i * 3.14)
                .append('\n');
        }
        return sb.toString();
    }

    static int countChar(String s, char target) {
        int count = 0;
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) == target) count++;
        }
        return count;
    }

    // -------------------------------------------------------------------------
    // 4. Stream / lambda — invokedynamic, lambda metafactory
    // -------------------------------------------------------------------------
    static long streamSum(List<Integer> list) {
        return list
            .stream()
            .filter(n -> n % 2 == 0)
            .mapToLong(Integer::longValue)
            .sum();
    }

    static Map<Integer, List<Integer>> groupByMod(List<Integer> list, int mod) {
        return list.stream().collect(Collectors.groupingBy(n -> n % mod));
    }

    // -------------------------------------------------------------------------
    // 5. Recursive fib with memo — OSR candidate for the iterative fallback,
    //    plus recursive call inlining
    // -------------------------------------------------------------------------
    static final Map<Integer, Long> memo = new HashMap<>();

    static long fib(int n) {
        if (n <= 1) return n;
        Long cached = memo.get(n);
        if (cached != null) return cached;
        long result = fib(n - 1) + fib(n - 2);
        memo.put(n, result);
        return result;
    }

    // -------------------------------------------------------------------------
    // 6. Sorting + comparator — gives C2 a comparator lambda to inline
    // -------------------------------------------------------------------------
    static List<String> sortByLength(List<String> words) {
        return words
            .stream()
            .sorted(
                Comparator.comparingInt(String::length).thenComparing(
                    Comparator.naturalOrder()
                )
            )
            .collect(Collectors.toList());
    }

    // -------------------------------------------------------------------------
    // Main — runs each workload enough times to cross the C2 threshold
    // (default CompileThreshold=10000 invocations)
    // -------------------------------------------------------------------------
    public static void main(String[] args) throws InterruptedException {
        final int WARMUP = 20_000; // well above C2 threshold

        System.out.println(
            "[TestWorkload] Starting — each section runs " +
                WARMUP +
                " iterations"
        );

        // --- Section 1: numeric loops ---
        int[] intArr = IntStream.range(0, 1000).toArray();
        double[] vecA = IntStream.range(0, 1000)
            .mapToDouble(i -> i * 0.1)
            .toArray();
        double[] vecB = IntStream.range(0, 1000)
            .mapToDouble(i -> i * 0.2)
            .toArray();
        long sumSink = 0;
        double dotSink = 0;

        for (int i = 0; i < WARMUP; i++) {
            sumSink += sumArray(intArr);
            dotSink += dotProduct(vecA, vecB);
        }
        System.out.printf(
            "[TestWorkload] numeric   sum=%d dot=%.2f%n",
            sumSink,
            dotSink
        );

        // --- Section 2: virtual dispatch ---
        Scorer sigmoid = new SigmoidScorer();
        Scorer tanh = new TanhScorer();
        double scoreSink = 0;

        for (int i = 0; i < WARMUP; i++) {
            // Alternate receivers — forces C2 to handle bimorphic call site
            scoreSink += runScorer(i % 2 == 0 ? sigmoid : tanh, 10);
        }
        System.out.printf("[TestWorkload] scorers   acc=%.4f%n", scoreSink);

        // --- Section 3: strings ---
        int charSink = 0;
        for (int i = 0; i < WARMUP; i++) {
            String csv = buildCsv(20);
            charSink += countChar(csv, ',');
        }
        System.out.printf("[TestWorkload] strings   commas=%d%n", charSink);

        // --- Section 4: streams ---
        List<Integer> numbers = IntStream.range(0, 500)
            .boxed()
            .collect(Collectors.toList());
        long streamSink = 0;
        for (int i = 0; i < WARMUP; i++) {
            streamSink += streamSum(numbers);
        }
        System.out.printf("[TestWorkload] streams   sum=%d%n", streamSink);

        // --- Section 5: recursive fib ---
        long fibSink = 0;
        for (int i = 0; i < WARMUP; i++) {
            fibSink += fib(30);
        }
        System.out.printf("[TestWorkload] fib(30)   result=%d%n", fibSink);

        // --- Section 6: sort ---
        List<String> words = List.of(
            "banana",
            "apple",
            "fig",
            "cherry",
            "date",
            "elderberry",
            "kiwi"
        );
        List<String> sortedSink = null;
        for (int i = 0; i < WARMUP; i++) {
            sortedSink = sortByLength(new ArrayList<>(words));
        }
        System.out.printf(
            "[TestWorkload] sorted    first=%s%n",
            sortedSink.get(0)
        );

        System.out.println("[TestWorkload] Exiting.");
    }
}
