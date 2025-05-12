var lcd = require("LCD1602").connect(I2C1, function() {
    console.log("LCD conectado");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Esperando datos...");
  });
  
var wifi = require("Wifi");
  wifi.connect ("TU_SSID", {password: "TU_CONTRASEÑA"}), function(err) {
  if (err) {
     console.log("Error al conectar a Wi-Fi", err);
      return;
    }
  }    
  lcd.print("Conectado a Wi-Fi");

var API_KEY = "TU_API_KEY"; 
var ciudad = "Madrid"; 
var url = "https://api.waqi.info/feed/" + ciudad + "/?token=" + API_KEY;
    
require("https").get(url, function(res) {
    var data = "";
    res.on("data", function(chunk) {
        data += chunk;
    });
      
    res.on("end", function() {
    var json = JSON.parse(data);
    if (json.status === "ok") {
      var aqi = json.data.aqi;
      var calidad = json.data.city.name;
  
      console.log("Calidad del aire en " + calidad + ": " + aqi);
          
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Calidad Aire:");
      lcd.setCursor(0, 1);
      lcd.print("AQI: " + aqi);
    } else {
      console.log("Error al obtener datos de la API");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Error");
    }
    });
      
    }).on('error', function(e) {
      console.log("Error al hacer la solicitud HTTP: " + e.message);
    });

  