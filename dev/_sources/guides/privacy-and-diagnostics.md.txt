# Privacy and diagnostics

This page says what EGTRAIN can send about its use and its errors, when it sends it, and how you switch it off.

## When anything is sent

EGTRAIN sends usage statistics and diagnostic reports only to a receiver whose exact HTTPS address is built into the program, and only after you agree. No receiver is configured today, so EGTRAIN sends nothing. Development builds and runs without a window, such as headless runs, tests and automation, never send anything, whatever the saved choices are.

When nothing can be sent, **Help > Privacy & diagnostics...** says `Usage statistics and diagnostics are unavailable in this build or run. Nothing is collected or sent.`

In a build with a receiver, EGTRAIN asks once in the dialog **Help improve EGTRAIN**. It has two separate choices, **Share usage statistics** and **Share diagnostic reports**. Both are ticked when the dialog opens, but neither takes effect until you click **Save choices**. **Not now**, Escape and closing the dialog switch both off. A new receiver address or new terms of collection ask again.

## What each choice sends

Both choices send the application version, the platform and the processor architecture with each batch of events. **Share usage statistics** sends these events:

- a session started;
- a case opened, as a bundled `.egscene` file or a local folder;
- an editor opened;
- a run started, completed or failed, with its duration in one of five ranges: under 1 s, 1 s to 10 s, 10 s to 1 minute, 1 to 10 minutes, or 10 minutes or more;
- an export completed or failed, with its kind: scene bundle, legacy, CSV or PNG.

Usage events carry a random installation ID. It counts installations that take part, not people. It is made when you switch usage statistics on, and removed when you switch them off; switching them on again makes a new one.

**Share diagnostic reports** sends one event when opening a case, a run or an export fails, with the kind of failure: invalid input, unsupported format, a file failure or an internal failure. Diagnostic events carry no installation ID and are sent separately from usage events.

Neither choice sends text from your cases, file names, paths, simulation input, logs, error messages, crash dumps, an email address or a device identifier. The receiver can see the IP address of your network. Events that cannot be sent wait on your computer for at most seven days.

The recovery copies that EGTRAIN keeps of unsaved edits stay on your computer and are never sent.

## Switch it off

1. Choose **Help > Privacy & diagnostics...**. The dialog shows your saved choices.
2. Untick **Share usage statistics**, **Share diagnostic reports** or both, and click **Save choices**.

Closing the dialog without saving keeps the saved choices. In a build without a receiver, as today, **Help > Privacy & diagnostics...** shows only the sentence of [When anything is sent](#when-anything-is-sent), and there is nothing to switch off. Events that were sent already cannot be recalled. EGTRAIN supplies no privacy notice yet.
