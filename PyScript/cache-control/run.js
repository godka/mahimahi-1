const CDP = require('chrome-remote-interface');


CDP((client) => {
    // extract domains
    const {Network, Page, Security} = client;
    // console.log(Security);

    Security.setIgnoreCertificateErrors({ignore: true});
    //Security.disable();

    // setup handlers
    if (process.argv[3] == "true"){
        Network.responseReceived ((params) => {
            console.log(`1\t${params.requestId}\t${params.response.url}`);
            const cacheControl1 = params.response.headers['cache-control'];
            const cacheControl2 = params.response.headers['Cache-Control'];
            // console.log(`*\t${cacheControl1}\t${cacheControl2}`);
            let cacheControl = null;
            if (cacheControl1 != null){
                cacheControl = cacheControl1;
            }  else if (cacheControl2 != null) {
                cacheControl = cacheControl2;
            }
            if (cacheControl != null) {
                const private = cacheControl.indexOf("private");
                const maxage = cacheControl.indexOf("max-age=");
                let bdigit = 0;
                if (maxage > -1) {
                    bdigit = parseInt(cacheControl[maxage + 8]);
                }
                if ( bdigit > 0 && private == -1) {
                    console.log(`2\t${params.requestId}`);
                }
            }
        });

        Network.loadingFinished ( (params) => {
            console.log(`3\t${params.requestId}\t${params.encodedDataLength}`);
        });
    } else {
        Network.requestWillBeSent((params) => {
            console.log(params.request.url);
        });
    }
    Page.loadEventFired(() => {
        client.close();
    });

    // enable events then start!
    Promise.all([
        Network.enable(),
        Page.enable()
    ]).then(() => {
        return Page.navigate({url: process.argv[2]});
    }).catch((err) => {
        console.error(err);
        client.close();
    });

}).on('error', (err) => {
    // cannot connect to the remote endpoint
    console.error(err);
});
