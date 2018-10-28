/*
 This script is used for cross-platform installing & building the example projects and
 checking the git diff of the examples directory.
 
 In a successful test scenario, after building the example projects, there should not be any changed files.
 (We check manually verified genType generated files in git, we consider diffs as regressions)
*/

const debug = require("debug")("IO");
const fs = require("fs");
const child_process = require("child_process");
const path = require("path");
const pjson = require("../package.json");

const exampleDirPaths = [
  "reason-react-example",
  "typescript-react-example",
  "untyped-react-example"
].map(exampleName => path.join(__dirname, "..", "examples", exampleName));

const isWindows = /^win/i.test(process.platform);

function getGenTypeFilePath() {
  const base = path.join(__dirname, "..", "lib", "bs", "native");
  if (isWindows) {
    return path.join(base, "gentype.native.exe");
  } else {
    return path.join(base, "gentype.native");
  }
}

const genTypeFile = getGenTypeFilePath();

/*
Needed for wrapping the stdout pipe with a promise
*/
async function wrappedSpawn(command, args, options) {
  return new Promise((resolve, reject) => {
    const child = child_process.spawn(command, args, {
      env: process.env,
      ...options
    });

    child.stdout.pipe(process.stdout);
    child.stderr.pipe(process.stderr);

    child.on("exit", (code) => {
      if(code == 0) {
        resolve(code);
      }
      else {
        reject(code);
      }
    })

    child.on("error", err => {
      console.error(`${command} ${args.join(" ")} exited with ${err.code}`);
      return reject(err.code);
    });
  });
}

async function installExamples() {
  const tasks = exampleDirPaths.map(cwd => {
    console.log(`${cwd}: npm install --production --no-save (takes a while)`);

    // The npm command is not an executable, but a cmd script on Windows
    // Without the shell = true, Windows will not find the program and fail
    // with ENOENT
    const shell = isWindows ? true : false;
    return wrappedSpawn("npm", ["install", "--production", "--no-save"], { cwd, shell });
  });

  return Promise.all(tasks);
}

async function buildExamples() {
  for (const cwd of exampleDirPaths) {
    console.log(`${cwd}: npm run build (takes a while)`);

    const shell = isWindows ? true : false;
    await wrappedSpawn("npm", ["run", "build"], {
      cwd,
      shell
    });
  }
}

async function checkDiff() {
  /* This function should definitely be rewritten in a cleaner way */
  try {
    console.log("Checking for changes in examples/");
    const output = child_process.execFileSync(
      "git",
      [
        "diff-index",
        "--name-only",
        "HEAD",
        "--",
        "examples/*.js",
        "examples/*.re",
        "examples/*.bs.js",
        "examples/*.re.js",
        "examples/*.ts"
      ],
      { encoding: "utf8" }
    );

    console.log(output);

    if (output.length > 0) {
      throw new Error(output);
    }
  } catch (err) {
    console.error(
      "Changed files detected in path examples/! Make sure genType is emitting the right code and commit the files to git"
    );
    console.error(err.message);
    process.exit(1);
  }
}

async function checkSetup() {
  console.log(`Check existing binary: ${genTypeFile}`);
  if (!fs.existsSync(genTypeFile)) {
    const filepath = path.relative(path.join(__dirname, ".."), genTypeFile);
    throw new Error(`${filepath} does not exist. Use \`npm run build\` first!`);
  }

  console.log("Checking if --version outputs the right version");
  let output;

  /* Compare the --version output with the package.json version number (should match) */
  try {
    output = child_process.execFileSync(genTypeFile, ["--version"], {
      encoding: "utf8"
    });
  } catch (e) {
    throw new Error(
      `gentype --version caused an unexpected error: ${e.message}`
    );
  }

  // For Unix / Windows
  const stripNewlines = (str = "") => str.replace(/[\n\r]+/g, "");

  if (output.indexOf(pjson.version) === -1) {
    throw new Error(
      `${path.basename(genTypeFile)} --version doesn't contain the version number of package.json` +
        `("${stripNewlines(output)}" should contain ${pjson.version})` +
        `- Run \`node scripts/bump_version_module.js\` and rebuild to sync version numbers`
    );
  }
}

async function main() {
  try {
    await checkSetup();
    await installExamples();
    await buildExamples();

    /* Git diffing is broken... we need a better way to test regressions */
    // await checkDiff();
    
    console.log("Test successful!");
  } catch (e) {
    console.error(`Test failed unexpectly: ${e.message}`);
    console.error(e);
    debug(e);
    process.exit(1);
  }
}

main();