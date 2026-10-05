// Optional local harness, using the Playwright library rather than test framework.
const fs=require('node:fs'),path=require('node:path');
const {chromium}=require(process.argv[2]||'playwright');
(async()=>{
  const browser=await chromium.launch({channel:'chrome',headless:true});
  const page=await browser.newPage({viewport:{width:1440,height:1000},acceptDownloads:true});
  page.setDefaultTimeout(10000);
  page.on('console',m=>{if(m.type()==='error')console.error('Browser console:',m.text());});
  try{
    await page.goto('http://127.0.0.1:8080/');
    const code=fs.readFileSync(path.join(__dirname,process.argv[3]||'browser_checks.js'),'utf8');
    const result=await (new Function('require','return '+code)(require))(page);
    console.log(JSON.stringify(result,null,2));
  } catch(error) {
    console.error(error.stack);
    await page.screenshot({path:'output/playwright/failure.png',fullPage:true});
    console.error('NOTICE:',await page.locator('#notice').textContent());
    process.exitCode=1;
  } finally { await browser.close(); }
})();
