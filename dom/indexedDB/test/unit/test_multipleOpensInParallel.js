/*
  Any copyright is dedicated to the Public Domain.
  http://creativecommons.org/publicdomain/zero/1.0/
*/

const { PrincipalUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/PrincipalUtils.sys.mjs"
);
const { IndexedDBUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/indexedDB/test/modules/IndexedDBUtils.sys.mjs"
);

/**
 * This is not a formal performance test — see the dedicated perf tests for
 * actual metrics.
 *
 * This test exists to:
 *   - Provide fast, local verification during development.
 *   - May detect early regressions in scheduling logic before perf test
 *     results are available.
 *
 * It simulates multiple concurrent open requests to the same database. Only
 * one should run at a time, and the rest should be scheduled efficiently.
 *
 * If scheduling or cleanup is slow, this test may fail due to timeout.
 *
 * Note: Not only the number of requests, but also the number of live (open)
 * databases may affect performance.
 *
 * Note: This test could theoretically be combined with its counterpart in a
 * single file with multiple tasks, but they are intentionally split to:
 *   - Avoid interference between test cases
 *   - Make it obvious which scenario timed out (serial vs. parallel)
 */

/* exported testSteps */
async function testSteps() {
  const principal = PrincipalUtils.createPrincipal("https://example.com");
  const name = "multipleOpensInParallel";

  info("Opening databases");

  {
    const startTime = ChromeUtils.now();

    const openPromises = [];

    for (let index = 0; index < 1000; index++) {
      const request = indexedDB.openForPrincipal(principal, name);
      const openPromise = IndexedDBUtils.requestFinished(request);
      openPromises.push(openPromise);
    }

    const databases = await Promise.all(openPromises);

    for (const database of databases) {
      database.close();
    }

    const endTime = ChromeUtils.now();

    const timeDelta = endTime - startTime;

    info(`Opened databases in ${timeDelta} msec`);
  }

  info("Deleting database");

  {
    const startTime = ChromeUtils.now();

    const request = indexedDB.deleteForPrincipal(principal, name);
    await IndexedDBUtils.requestFinished(request);

    const endTime = ChromeUtils.now();

    const timeDelta = endTime - startTime;

    info(`Deleted database in ${timeDelta} msec`);
  }
}
