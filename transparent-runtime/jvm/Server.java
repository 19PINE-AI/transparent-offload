import java.io.*;
import java.net.*;

// Classic thread-per-connection blocking server (platform threads): one new OS thread per
// accepted connection -> 1:1 connection->fiber under libtransparent. Light per-request CPU.
public class Server {
    static int SPIN_US = 0;           // optional CPU work per request (microseconds)
    public static void main(String[] a) throws Exception {
        int port = a.length>0 ? Integer.parseInt(a[0]) : 7730;
        if (a.length>1) SPIN_US = Integer.parseInt(a[1]);
        ServerSocket ss = new ServerSocket(port, 1024);
        System.out.println("java-server listening on " + port + " spin_us=" + SPIN_US);
        for (;;) {
            Socket s = ss.accept();
            new Thread(() -> handle(s)).start();   // platform thread per connection
        }
    }
    static void handle(Socket s) {
        try {
            s.setTcpNoDelay(true);
            InputStream in = s.getInputStream();
            OutputStream out = s.getOutputStream();
            byte[] buf = new byte[8192];
            // read one HTTP request (until CRLFCRLF or EOF)
            int n, total=0; boolean got=false;
            while ((n = in.read(buf, total, buf.length-total)) > 0) {
                total += n;
                String sofar = new String(buf, 0, total);
                if (sofar.contains("\r\n\r\n")) { got=true; break; }
                if (total >= buf.length) break;
            }
            if (SPIN_US > 0) { long end = System.nanoTime() + SPIN_US*1000L; while (System.nanoTime() < end); }
            byte[] body = "ok-java\n".getBytes();
            String hdr = "HTTP/1.1 200 OK\r\nContent-Length: "+body.length+"\r\nConnection: close\r\n\r\n";
            out.write(hdr.getBytes()); out.write(body); out.flush();
            s.close();
        } catch (IOException e) { try { s.close(); } catch (IOException x) {} }
    }
}
