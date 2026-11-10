package dashboard

import (
	"context"
	"encoding/xml"
	"os"
	"os/exec"
	"sort"
	"strings"
	"time"
)

// TestCase is a single Catch2 test discovered via `--list-tests`.
type TestCase struct {
	Name       string
	Tags       string
	Categories []string
}

// CategoryCount stores a category name and the number of testcases under it.
type CategoryCount struct {
	Name  string
	Count int
}

type catch2XMLMatchingTests struct {
	XMLName   xml.Name        `xml:"MatchingTests"`
	TestCases []catch2XMLTest `xml:"TestCase"`
}

type catch2XMLTest struct {
	Name string `xml:"Name"`
	Tags string `xml:"Tags"`
}

// ExtractTags parses raw Catch2 tag strings like "[A][B][C]" into a clean slice ["A", "B", "C"].
func ExtractTags(raw string) []string {
	var tags []string
	start := -1
	for i, r := range raw {
		if r == '[' {
			start = i + 1
		} else if r == ']' && start != -1 {
			tag := strings.TrimSpace(raw[start:i])
			if tag != "" {
				tags = append(tags, tag)
			}
			start = -1
		}
	}
	return tags
}

// ListCatch2Tests runs the given test binary to discover test cases.
// It prefers structured XML via `--list-tests -r xml`, and falls back to
// text parsing if XML is unavailable.
func ListCatch2Tests(binPath string) ([]TestCase, []CategoryCount, error) {
	if _, err := os.Stat(binPath); err != nil {
		return nil, nil, err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	// Try XML first (accurate names without line wrapping)
	cmdXML := exec.CommandContext(ctx, binPath, "--list-tests", "-r", "xml")
	cmdXML.Dir = filepathDir(binPath)
	if out, err := cmdXML.Output(); err == nil {
		if cases := parseCatch2XML(out); len(cases) > 0 {
			cats := aggregateCategories(cases)
			return cases, cats, nil
		}
	}

	// Fallback to text parsing
	cmdText := exec.CommandContext(ctx, binPath, "--list-tests")
	cmdText.Dir = filepathDir(binPath)
	out, err := cmdText.Output()
	if err != nil {
		return nil, nil, err
	}
	cases := parseCatch2List(string(out))
	cats := aggregateCategories(cases)
	return cases, cats, nil
}

func parseCatch2XML(data []byte) []TestCase {
	var doc catch2XMLMatchingTests
	if err := xml.Unmarshal(data, &doc); err != nil {
		return nil
	}
	out := make([]TestCase, 0, len(doc.TestCases))
	for _, tc := range doc.TestCases {
		name := strings.TrimSpace(tc.Name)
		if name == "" {
			continue
		}
		tags := strings.TrimSpace(tc.Tags)
		out = append(out, TestCase{
			Name:       name,
			Tags:       tags,
			Categories: ExtractTags(tags),
		})
	}
	return out
}

func parseCatch2List(text string) []TestCase {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	lines := strings.Split(text, "\n")
	var out []TestCase
	var pending *TestCase
	for _, line := range lines {
		if line == "" {
			pending = nil
			continue
		}
		// Footer "N test cases" / "N matching test cases".
		trimmed := strings.TrimSpace(line)
		if strings.HasSuffix(trimmed, "test cases") || strings.HasSuffix(trimmed, "test case") {
			pending = nil
			continue
		}
		indent := countLeadingSpaces(line)
		switch {
		case indent == 2:
			name := strings.TrimSpace(line)
			out = append(out, TestCase{Name: name})
			pending = &out[len(out)-1]
		case indent >= 4 && pending != nil:
			tags := strings.TrimSpace(line)
			if pending.Tags == "" {
				pending.Tags = tags
			} else {
				pending.Tags += " " + tags
			}
			pending.Categories = ExtractTags(pending.Tags)
		default:
			pending = nil
		}
	}
	return out
}

func aggregateCategories(cases []TestCase) []CategoryCount {
	counts := make(map[string]int)
	for _, tc := range cases {
		seen := make(map[string]struct{}, len(tc.Categories))
		for _, cat := range tc.Categories {
			if _, ok := seen[cat]; !ok {
				seen[cat] = struct{}{}
				counts[cat]++
			}
		}
	}
	res := make([]CategoryCount, 0, len(counts))
	for cat, count := range counts {
		res = append(res, CategoryCount{Name: cat, Count: count})
	}
	sort.Slice(res, func(i, j int) bool {
		return strings.ToLower(res[i].Name) < strings.ToLower(res[j].Name)
	})
	return res
}

func countLeadingSpaces(s string) int {
	n := 0
	for n < len(s) && s[n] == ' ' {
		n++
	}
	return n
}

// filepathDir is a tiny helper to avoid pulling in path/filepath at package top
// just for one call site (the binary's directory is used as the test cwd so
// any --reporter file paths stay near the binary).
func filepathDir(p string) string {
	for i := len(p) - 1; i >= 0; i-- {
		if p[i] == '/' || p[i] == '\\' {
			return p[:i]
		}
	}
	return "."
}
