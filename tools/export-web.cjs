// Keep the old entry point from accidentally restoring a Slimbuck game bundle.
console.error('Website exports have retired. Use node tools/publish-web.cjs --check to test, then node tools/publish-web.cjs to publish chirky.org.');
process.exitCode=1;
